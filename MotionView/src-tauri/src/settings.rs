use std::path::PathBuf;

use tauri::{AppHandle, Manager};
use base64::Engine as _;

const SETTINGS_FILE: &str = "user-preferences.json";

fn settings_path(app: &AppHandle) -> Result<PathBuf, String> {
    let dir = app
        .path()
        .app_config_dir()
        .map_err(|e: tauri::Error| e.to_string())?;
    std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    Ok(dir.join(SETTINGS_FILE))
}

#[tauri::command]
pub fn read_settings(app: AppHandle) -> Result<Option<String>, String> {
    let path = settings_path(&app)?;
    if !path.exists() {
        return Ok(None);
    }
    std::fs::read_to_string(path)
        .map(Some)
        .map_err(|e| e.to_string())
}

#[tauri::command]
pub fn write_settings(app: AppHandle, contents: String) -> Result<(), String> {
    serde_json::from_str::<serde_json::Value>(&contents).map_err(|e| e.to_string())?;
    let path = settings_path(&app)?;
    std::fs::write(path, contents).map_err(|e| e.to_string())
}

fn mime_from_ext(path: &std::path::Path) -> &'static str {
    match path.extension().and_then(|s| s.to_str()).unwrap_or("").to_lowercase().as_str() {
        "png" => "image/png",
        "jpg" | "jpeg" => "image/jpeg",
        "gif" => "image/gif",
        "webp" => "image/webp",
        "bmp" => "image/bmp",
        "svg" => "image/svg+xml",
        _ => "application/octet-stream",
    }
}

#[tauri::command]
pub fn read_image_data(path: String) -> Result<String, String> {
    let p = std::path::PathBuf::from(path);
    if !p.exists() {
        return Err("image path does not exist".into());
    }
    if !p.is_file() {
        return Err("image path is not a file".into());
    }
    let bytes = std::fs::read(&p).map_err(|e| e.to_string())?;
    let mime = mime_from_ext(&p);
    let b64 = base64::engine::general_purpose::STANDARD.encode(bytes);
    Ok(format!("data:{mime};base64,{b64}"))
}
