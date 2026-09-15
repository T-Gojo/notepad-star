use crate::{
    EditProposal, ErrorCode, ExtensionError, Result, INSTRUCTION_FUEL, MAX_MEMORY_BYTES,
    MAX_OUTPUT_BYTES,
};
use wasmi::{
    core::{LimiterError, ResourceLimiter, TrapCode, ValType},
    Config, EnforcedLimits, Engine, ExternType, Linker, Memory, Module, StackLimits, Store,
    StoreLimits, StoreLimitsBuilder, TypedFunc,
};

struct Budget {
    limits: StoreLimits,
    locked: bool,
    denied: bool,
}

impl ResourceLimiter for Budget {
    fn memory_growing(
        &mut self,
        current: usize,
        desired: usize,
        maximum: Option<usize>,
    ) -> std::result::Result<bool, LimiterError> {
        if self.locked && desired > current {
            self.denied = true;
            return Err(LimiterError::ResourceLimiterDeniedAllocation);
        }
        let result = self.limits.memory_growing(current, desired, maximum);
        self.denied |= !matches!(result, Ok(true));
        result
    }

    fn table_growing(
        &mut self,
        current: usize,
        desired: usize,
        maximum: Option<usize>,
    ) -> std::result::Result<bool, LimiterError> {
        if self.locked && desired > current {
            self.denied = true;
            return Err(LimiterError::ResourceLimiterDeniedAllocation);
        }
        let result = self.limits.table_growing(current, desired, maximum);
        self.denied |= !matches!(result, Ok(true));
        result
    }

    fn instances(&self) -> usize {
        1
    }
    fn tables(&self) -> usize {
        1
    }
    fn memories(&self) -> usize {
        1
    }
}

pub(crate) struct Guest {
    store: Store<Budget>,
    memory: Memory,
    alloc: TypedFunc<i32, i32>,
    transform: TypedFunc<(i32, i32), i64>,
}

impl Guest {
    pub fn new(bytes: &[u8]) -> Result<Self> {
        reject_growth_instructions(bytes)?;
        let mut config = Config::default();
        config
            .consume_fuel(true)
            .wasm_multi_memory(false)
            .wasm_memory64(false)
            .wasm_custom_page_sizes(false)
            .ignore_custom_sections(true)
            .enforced_limits(EnforcedLimits::strict())
            .set_stack_limits(StackLimits {
                initial_value_stack_height: 1024,
                maximum_value_stack_height: 65_536,
                maximum_recursion_depth: 128,
            });
        let engine = Engine::new(&config);
        let module = Module::new(&engine, bytes).map_err(|_| {
            ExtensionError::new(
                ErrorCode::InvalidModule,
                "Invalid or unsupported Wasm module.",
            )
        })?;
        if module.imports().next().is_some() {
            return Err(ExtensionError::new(
                ErrorCode::ImportsForbidden,
                "All imports, including WASI and host functions, are forbidden.",
            ));
        }
        let invalid_exports = || {
            ExtensionError::new(
                ErrorCode::InvalidExports,
                "ABI v1 requires memory, alloc(i32)->i32, and transform(i32,i32)->i64.",
            )
        };
        let Some(ExternType::Memory(memory_type)) = module.get_export("memory") else {
            return Err(invalid_exports());
        };
        if memory_type.is_64() {
            return Err(invalid_exports());
        }
        if memory_type.minimum() > (MAX_MEMORY_BYTES / 65_536) as u64 {
            return Err(ExtensionError::new(
                ErrorCode::MemoryLimit,
                "Initial linear memory exceeds the byte limit.",
            ));
        }
        for (name, params, results) in [
            ("alloc", &[ValType::I32][..], &[ValType::I32][..]),
            (
                "transform",
                &[ValType::I32, ValType::I32][..],
                &[ValType::I64][..],
            ),
        ] {
            let Some(ExternType::Func(function)) = module.get_export(name) else {
                return Err(invalid_exports());
            };
            if function.params() != params || function.results() != results {
                return Err(invalid_exports());
            }
        }
        let limits = StoreLimitsBuilder::new()
            .memory_size(MAX_MEMORY_BYTES)
            .table_elements(1024)
            .instances(1)
            .memories(1)
            .tables(1)
            .trap_on_grow_failure(true)
            .build();
        let mut store = Store::new(
            &engine,
            Budget {
                limits,
                locked: false,
                denied: false,
            },
        );
        store.limiter(|budget| budget);
        store.set_fuel(INSTRUCTION_FUEL).map_err(|_| {
            ExtensionError::new(ErrorCode::Trap, "Could not initialize the fuel budget.")
        })?;
        let pre = Linker::<Budget>::new(&engine)
            .instantiate(&mut store, &module)
            .map_err(|error| classify(&store, error))?;
        let instance = pre.ensure_no_start(&mut store).map_err(|_| {
            ExtensionError::new(
                ErrorCode::StartForbidden,
                "Wasm start functions are forbidden by ABI v1.",
            )
        })?;
        store.data_mut().locked = true;
        let memory = instance
            .get_memory(&store, "memory")
            .ok_or_else(invalid_exports)?;
        let alloc = instance
            .get_typed_func::<i32, i32>(&store, "alloc")
            .map_err(|_| invalid_exports())?;
        let transform = instance
            .get_typed_func::<(i32, i32), i64>(&store, "transform")
            .map_err(|_| invalid_exports())?;
        Ok(Self {
            store,
            memory,
            alloc,
            transform,
        })
    }

    pub fn transform(&mut self, input: &str) -> Result<EditProposal> {
        let len = i32::try_from(input.len()).map_err(|_| {
            ExtensionError::new(ErrorCode::TooLarge, "Input does not fit the ABI length.")
        })?;
        let ptr = self
            .alloc
            .call(&mut self.store, len)
            .map_err(|error| classify(&self.store, error))?;
        let offset = ptr as u32 as usize;
        checked_range(offset, input.len(), self.memory.data(&self.store).len())?;
        self.memory
            .write(&mut self.store, offset, input.as_bytes())
            .map_err(|_| invalid_range())?;
        let packed = self
            .transform
            .call(&mut self.store, (ptr, len))
            .map_err(|error| classify(&self.store, error))? as u64;
        let output_ptr = (packed >> 32) as u32 as usize;
        let output_len = packed as u32 as usize;
        if output_len > MAX_OUTPUT_BYTES {
            return Err(ExtensionError::new(
                ErrorCode::OutputTooLarge,
                "Extension output exceeds the byte limit.",
            ));
        }
        let bytes = self.memory.data(&self.store);
        let range = checked_range(output_ptr, output_len, bytes.len())?;
        let text = std::str::from_utf8(&bytes[range]).map_err(|_| {
            ExtensionError::new(
                ErrorCode::InvalidUtf8,
                "Extension output is not valid UTF-8.",
            )
        })?;
        Ok(EditProposal {
            replacement: text.to_owned(),
        })
    }
}

fn checked_range(start: usize, len: usize, size: usize) -> Result<std::ops::Range<usize>> {
    let end = start.checked_add(len).ok_or_else(invalid_range)?;
    if start > size || end > size {
        return Err(invalid_range());
    }
    Ok(start..end)
}

fn invalid_range() -> ExtensionError {
    ExtensionError::new(
        ErrorCode::InvalidMemoryRange,
        "Extension returned an out-of-bounds memory pointer or length.",
    )
}

fn reject_growth_instructions(bytes: &[u8]) -> Result<()> {
    let invalid = |_| {
        ExtensionError::new(
            ErrorCode::InvalidModule,
            "Invalid or unsupported Wasm module.",
        )
    };
    for payload in wasmparser::Parser::new(0).parse_all(bytes) {
        if let wasmparser::Payload::CodeSectionEntry(body) = payload.map_err(invalid)? {
            let mut operators = body.get_operators_reader().map_err(invalid)?;
            while !operators.eof() {
                if matches!(
                    operators.read().map_err(invalid)?,
                    wasmparser::Operator::MemoryGrow { .. }
                        | wasmparser::Operator::TableGrow { .. }
                ) {
                    // Wasm may return -1 before invoking a resource limiter when
                    // growth exceeds a declared maximum. Reject the operators
                    // too, so a guest cannot swallow that failure as success.
                    return Err(ExtensionError::new(
                        ErrorCode::MemoryLimit,
                        "Memory and table growth instructions are forbidden by ABI v1.",
                    ));
                }
            }
        }
    }
    Ok(())
}

fn classify(store: &Store<Budget>, error: wasmi::Error) -> ExtensionError {
    if store.data().denied {
        ExtensionError::new(
            ErrorCode::MemoryLimit,
            "Memory or table allocation/growth exceeded the budget.",
        )
    } else if error.as_trap_code() == Some(TrapCode::OutOfFuel) {
        ExtensionError::new(
            ErrorCode::FuelExhausted,
            "Extension exhausted its instruction fuel budget.",
        )
    } else {
        ExtensionError::new(
            ErrorCode::Trap,
            "Extension trapped or exceeded a store limit.",
        )
    }
}
