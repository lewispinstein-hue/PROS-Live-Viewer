use std::{
  io,
  net::TcpListener,
  path::PathBuf,
  process::{Child, Command},
  sync::Mutex,
};

use tauri::{Manager, RunEvent};
use tauri::AppHandle;
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

fn project_root(handle: &AppHandle) -> tauri::Result<PathBuf> {
  #[cfg(debug_assertions)]
  {
    // If src-tauri is a subdir, repo root is parent of CARGO_MANIFEST_DIR.
    Ok(PathBuf::from(env!("CARGO_MANIFEST_DIR"))
      .parent()
      .unwrap()
      .to_path_buf())
  }

  #[cfg(not(debug_assertions))]
  {
    Ok(handle.path().resource_dir()?)
  }
}

fn stop_bridge(state: &tauri::State<BridgeState>) {
  if let Some(mut child) = state.0.lock().unwrap().take() {
    let _ = child.kill();
    let _ = child.wait(); // avoid zombie
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
      settings::read_image_data
    ])
    .setup(|app| {
      let port = pick_free_port();
      let child = spawn_bridge(app.handle(), port)?;
      *app.state::<BridgeState>().0.lock().unwrap() = Some(child);

      // Tell frontend where backend is
      if let Some(win) = app.get_webview_window("main") {
        win.eval(&format!(
          "window.__BRIDGE_ORIGIN__ = 'http://127.0.0.1:{port}';"
        ))?;
      }
      Ok(())
    })
    .build(tauri::generate_context!())
    .expect("error building tauri app")
    .run(|app_handle, event| {
      if let RunEvent::ExitRequested { .. } = event {
        if let Some(mut child) = app_handle.state::<BridgeState>().0.lock().unwrap().take() {
          let _ = child.kill();
        }
      match event {
        // Fires when any window is closed/destroyed (covers clicking the red X)
        RunEvent::WindowEvent { label, event, .. } => {
          if label == "main" {
            if matches!(event, tauri::WindowEvent::Destroyed) {
              stop_bridge(&app_handle.state::<BridgeState>());
            }
          }
        }

        // Fires when the app is exiting normally
        RunEvent::Exit => {
          stop_bridge(&app_handle.state::<BridgeState>());
        }

        // Fires on quit requests (Cmd+Q / Dock Quit / menu Quit)
        RunEvent::ExitRequested { .. } => {
          stop_bridge(&app_handle.state::<BridgeState>());
        }

        _ => {}
      }
    }
    });
}