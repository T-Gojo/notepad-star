#![cfg_attr(target_os = "windows", windows_subsystem = "windows")]

fn main() -> std::process::ExitCode {
    if std::env::args_os()
        .nth(1)
        .is_some_and(|mode| mode == "--instance-client-worker")
    {
        return match star_native::instance_client_worker() {
            Ok(()) => std::process::ExitCode::SUCCESS,
            Err(error) => {
                eprintln!("{error}");
                std::process::ExitCode::FAILURE
            }
        };
    }
    if std::env::args_os()
        .nth(1)
        .is_some_and(|mode| mode == "--manage-extensions-worker")
    {
        return match star_native::extension_management_worker() {
            Ok(()) => std::process::ExitCode::SUCCESS,
            Err(error) => {
                eprintln!("{error}");
                std::process::ExitCode::FAILURE
            }
        };
    }
    if std::env::args_os()
        .nth(1)
        .is_some_and(|mode| mode == "--outline-worker")
    {
        return match star_native::outline_worker() {
            Ok(()) => std::process::ExitCode::SUCCESS,
            Err(error) => {
                eprintln!("{error}");
                std::process::ExitCode::FAILURE
            }
        };
    }
    if let Some(mode) = std::env::args_os()
        .nth(1)
        .and_then(|arg| arg.into_string().ok())
        .filter(|arg| {
            arg == "--disk-apply-worker"
                || arg == "--disk-restore-worker"
                || arg == "--disk-inspect-worker"
        })
    {
        return match star_native::disk_worker(&mode) {
            Ok(()) => std::process::ExitCode::SUCCESS,
            Err(error) => {
                eprintln!("{error}");
                std::process::ExitCode::FAILURE
            }
        };
    }
    if let Some(mode) = std::env::args_os()
        .nth(1)
        .filter(|arg| arg == "--extension-worker" || arg == "--inspect-extension-worker")
    {
        return match star_native::extension_worker(mode == "--inspect-extension-worker") {
            Ok(()) => std::process::ExitCode::SUCCESS,
            Err(error) => {
                eprintln!("{error}");
                std::process::ExitCode::FAILURE
            }
        };
    }
    let worker = std::env::args_os().nth(1);
    if worker
        .as_ref()
        .is_some_and(|arg| arg == "--search-worker" || arg == "--list-files-worker")
    {
        return match star_native::worker(worker.is_some_and(|arg| arg == "--list-files-worker")) {
            Ok(()) => std::process::ExitCode::SUCCESS,
            Err(error) => {
                eprintln!("{error}");
                std::process::ExitCode::FAILURE
            }
        };
    }
    let options = match star_core::launch::parse(std::env::args_os().skip(1)) {
        Ok(options) => options,
        Err(error) => {
            eprintln!("{error}");
            return std::process::ExitCode::FAILURE;
        }
    };
    if options.help {
        println!("{}", star_core::launch::HELP);
        return std::process::ExitCode::SUCCESS;
    }
    if options.version {
        println!(
            "Notepad Star {} ({})",
            star_core::VERSION,
            if star_core::DEVELOPMENT_BUILD {
                "development"
            } else {
                "release candidate"
            }
        );
        return std::process::ExitCode::SUCCESS;
    }
    match star_native::run(&options) {
        Ok(0) => std::process::ExitCode::SUCCESS,
        Ok(code) => {
            eprintln!("Desktop exited with status {code}.");
            std::process::ExitCode::FAILURE
        }
        Err(error) => {
            eprintln!("Desktop initialization failed: {error}");
            std::process::ExitCode::FAILURE
        }
    }
}
