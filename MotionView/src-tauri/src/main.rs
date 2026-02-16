use std::{
  io,
  net::TcpListener,
  path::PathBuf,
  process::{Child, Command},
  sync::Mutex,
};
use std::{fs, process::Stdio};

use tauri::{Manager, RunEvent};
use tauri::path::BaseDirectory;

mod settings;

struct BridgeState(Mutex<Option<Child>>);

fn pick_free_port() -> u16 {
  let l = TcpListener::bind(("127.0.0.1", 0)).expect("bind 127.0.0.1:0");
  l.local_addr().unwrap().port()
}

fn resolve_bridge(app: &tauri::AppHandle) -> tauri::Result<PathBuf> {
  let candidates = ["_up_/src/bridge.py", "_up_/bridge.py", "bridge.py", "src/bridge.py"];

  for rel in candidates {
    let p = app.path().resolve(rel, BaseDirectory::Resource)?;
    if p.exists() {
      return Ok(p);
    }
  }

  Err(tauri::Error::Io(io::Error::new(
    io::ErrorKind::NotFound,
    "bridge.py not found in bundle resources",
  )))
}

fn stop_bridge(state: &tauri::State<BridgeState>, app: &tauri::AppHandle) {
  if let Some(mut child) = state.0.lock().unwrap().take() {
    let _ = child.kill();
    let _ = child.wait(); // avoid zombie
  }
  if let Ok(path) = pid_path(app) {
    let _ = fs::remove_file(path);
  }
}

fn pid_path(app: &tauri::AppHandle) -> Result<PathBuf, tauri::Error> {
  app.path()
    .app_data_dir()
    .map(|dir| dir.join("bridge.pid"))
}

#[cfg(unix)]
fn kill_pid(pid: u32) {
  let _ = Command::new("kill")
    .arg("-9")
    .arg(pid.to_string())
    .stdin(Stdio::null())
    .stdout(Stdio::null())
    .stderr(Stdio::null())
    .status();
}

#[cfg(windows)]
fn kill_pid(pid: u32) {
  let _ = Command::new("taskkill")
    .args(["/PID", &pid.to_string(), "/T", "/F"])
    .stdin(Stdio::null())
    .stdout(Stdio::null())
    .stderr(Stdio::null())
    .status();
}

fn cleanup_previous_bridge(app: &tauri::AppHandle) {
  let path = match pid_path(app) {
    Ok(p) => p,
    Err(_) => return,
  };
  let pid_str = match fs::read_to_string(&path) {
    Ok(s) => s,
    Err(_) => return,
  };
  if let Ok(pid) = pid_str.trim().parse::<u32>() {
    kill_pid(pid);
  }
  let _ = fs::remove_file(path);
}

fn write_bridge_pid(app: &tauri::AppHandle, pid: u32) {
  if let Ok(path) = pid_path(app) {
    if let Some(parent) = path.parent() {
      let _ = fs::create_dir_all(parent);
    }
    let _ = fs::write(path, pid.to_string());
  }
}

fn spawn_bridge(app: &tauri::AppHandle, port: u16) -> Result<Child, tauri::Error> {
  let script = resolve_bridge(app)?;
  let workdir = script.parent().unwrap().to_path_buf();

  let py = ["/usr/bin/python3", "/opt/homebrew/bin/python3", "/usr/local/bin/python3"]
    .into_iter()
    .map(PathBuf::from)
    .find(|p| p.exists())
    .unwrap_or_else(|| PathBuf::from("python3"));

  Command::new(py)
    .arg(&script)
    .args(["--host", "127.0.0.1", "--port", &port.to_string()])
    .current_dir(&workdir)
    .spawn()
    .map_err(tauri::Error::Io)
}

fn main() {
  tauri::Builder::default()
    .manage(BridgeState(Mutex::new(None)))
    .invoke_handler(tauri::generate_handler![
      settings::read_settings,
      settings::write_settings,
      settings::read_image_data,
      settings::save_robot_image,
      settings::read_saved_paths,
      settings::write_saved_paths
    ])
    .setup(|app| {
      cleanup_previous_bridge(app.handle());
      let port = pick_free_port();
      let child = spawn_bridge(app.handle(), port)?;
      write_bridge_pid(app.handle(), child.id());
      *app.state::<BridgeState>().0.lock().unwrap() = Some(child);

      // Tell frontend where backend is
      if let Some(win) = app.get_webview_window("main") {
        win.eval(&format!(
          "window.__BRIDGE_ORIGIN__ = 'http://127.0.0.1:{port}';"
        ))?;

        #[cfg(not(mobile))]
        {
          if let Ok(Some(state)) = settings::read_window_state(app.handle()) {
            if state.fullscreen {
              let _ = win.set_fullscreen(true);
            } else {
              let _ = win.set_size(tauri::Size::Physical(tauri::PhysicalSize::new(state.width, state.height)));
              let _ = win.set_position(tauri::Position::Physical(tauri::PhysicalPosition::new(state.x, state.y)));
            }
          }
        }
      }
      Ok(())
    })
    .build(tauri::generate_context!())
    .expect("error building tauri app")
    .run(|app_handle, event| {
      match event {
        // Fires when any window is closed/destroyed (covers clicking the red X)
        RunEvent::WindowEvent { label, event, .. } => {
          if label == "main" {
            match event {
              tauri::WindowEvent::CloseRequested { .. } => {
                if let Some(win) = app_handle.get_webview_window("main") {
                  if let Err(err) = settings::save_window_state(&app_handle, &win) {
                    eprintln!("Failed to save window state: {err}");
                  }
                }
              }
              tauri::WindowEvent::Destroyed => {
                stop_bridge(&app_handle.state::<BridgeState>(), app_handle);
              }
              _ => {}
            }
          }
        }

        // Fires when the app is exiting normally
        RunEvent::Exit => {
          if let Some(win) = app_handle.get_webview_window("main") {
            if let Err(err) = settings::save_window_state(&app_handle, &win) {
              eprintln!("Failed to save window state: {err}");
            }
          }
          stop_bridge(&app_handle.state::<BridgeState>(), app_handle);
        }

        // Fires on quit requests (Cmd+Q / Dock Quit / menu Quit)
        RunEvent::ExitRequested { .. } => {
          if let Some(win) = app_handle.get_webview_window("main") {
            if let Err(err) = settings::save_window_state(&app_handle, &win) {
              eprintln!("Failed to save window state: {err}");
            }
          }
          stop_bridge(&app_handle.state::<BridgeState>(), app_handle);
        }

        _ => {}
      }
    });
}
