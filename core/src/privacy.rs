//! Privacy engine: fingerprint protection scripts, tracking-parameter stripping,
//! HTTPS-first upgrades. All pure functions — the shell injects/enforces.

/// Fingerprint protection level.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum FingerprintLevel {
    Off,
    Standard,
    Strict,
}

impl FingerprintLevel {
    pub fn from_str(s: &str) -> Self {
        match s {
            "strict" => FingerprintLevel::Strict,
            "off" => FingerprintLevel::Off,
            _ => FingerprintLevel::Standard,
        }
    }
    pub fn as_str(&self) -> &'static str {
        match self {
            FingerprintLevel::Off => "off",
            FingerprintLevel::Standard => "standard",
            FingerprintLevel::Strict => "strict",
        }
    }
}

/// Deterministic per-session, per-site seed (Brave-style farbling, lightweight).
fn farble_seed(host: &str) -> u32 {
    let mut h: u32 = 0x811c9dc5;
    for b in host.bytes() {
        h ^= b as u32;
        h = h.wrapping_mul(0x01000193);
    }
    h
}

/// Generate the fingerprint-protection user script for the given level + host.
/// Injected at document-start into every page (per-webview UserContentManager).
pub fn fingerprint_script(level: FingerprintLevel, host: &str) -> String {
    if level == FingerprintLevel::Off {
        return String::new();
    }
    let seed = farble_seed(host);
    let strict = level == FingerprintLevel::Strict;
    // Canvas + WebGL + Audio + font metrics farbling. Kept dependency-free (IIFE).
    let mut js = String::with_capacity(4096);
    js.push_str("(function(){\n'use strict';\n");
    js.push_str(&format!("var __KSEED={seed}u|0;\n"));
    js.push_str(r#"
function krand(){ __KSEED ^= __KSEED<<13; __KSEED ^= __KSEED>>>17; __KSEED ^= __KSEED<<5; return ((__KSEED>>>0)/4294967296); }
try {
  var _td = HTMLCanvasElement.prototype.toDataURL;
  HTMLCanvasElement.prototype.toDataURL = function(){ 
    try { var c=this.getContext('2d'); if(c&&this.width>0&&this.height>0){ var d=c.getImageData(0,0,Math.min(this.width,16),Math.min(this.height,16)); for(var i=3;i<d.data.length;i+=4*7){ d.data[i]=d.data[i]^(krand()<0.5?1:0);} c.putImageData(d,0,0);} } catch(e){}
    return _td.apply(this,arguments); };
  var _tb = HTMLCanvasElement.prototype.toBlob;
  HTMLCanvasElement.prototype.toBlob = function(cb){
    try { var c=this.getContext('2d'); if(c&&this.width>0&&this.height>0){ var d=c.getImageData(0,0,Math.min(this.width,16),Math.min(this.height,16)); for(var i=3;i<d.data.length;i+=4*7){ d.data[i]=d.data[i]^(krand()<0.5?1:0);} c.putImageData(d,0,0);} } catch(e){}
    return _tb.apply(this,arguments); };
  var _gid = CanvasRenderingContext2D.prototype.getImageData;
  CanvasRenderingContext2D.prototype.getImageData = function(x,y,w,h){
    var r = _gid.call(this,x,y,w,h);
    try { for(var i=3;i<r.data.length;i+=4*7){ r.data[i]=r.data[i]^(krand()<0.5?1:0);} } catch(e){}
    return r; };
} catch(e) {}
try {
  var _gv = WebGLRenderingContext.prototype.getParameter;
  WebGLRenderingContext.prototype.getParameter = function(p){
    var ext = this.getExtension('WEBGL_debug_renderer_info');
    if (ext && (p === ext.UNMASKED_VENDOR_WEBGL)) return 'Kestrel';
    if (ext && (p === ext.UNMASKED_RENDERER_WEBGL)) return 'Kestrel GPU (protected)';
    return _gv.call(this,p); };
  if (window.WebGL2RenderingContext) {
    var _gv2 = WebGL2RenderingContext.prototype.getParameter;
    WebGL2RenderingContext.prototype.getParameter = function(p){
      var ext = this.getExtension('WEBGL_debug_renderer_info');
      if (ext && (p === ext.UNMASKED_VENDOR_WEBGL)) return 'Kestrel';
      if (ext && (p === ext.UNMASKED_RENDERER_WEBGL)) return 'Kestrel GPU (protected)';
      return _gv2.call(this,p); };
  }
} catch(e) {}
try {
  var _gfd = AnalyserNode.prototype.getFloatFrequencyData;
  AnalyserNode.prototype.getFloatFrequencyData = function(a){
    _gfd.call(this,a); for (var i=0;i<a.length;i+=13) a[i] += (krand()-0.5)*0.0001; };
  var _gct = AnalyserNode.prototype.getByteFrequencyData;
  AnalyserNode.prototype.getByteFrequencyData = function(a){
    _gct.call(this,a); for (var i=0;i<a.length;i+=17) a[i] = Math.max(0, a[i]-1); };
} catch(e) {}
try { Object.defineProperty(navigator, 'hardwareConcurrency', { get: function(){ return 2; } }); } catch(e) {}
try { Object.defineProperty(navigator, 'deviceMemory', { get: function(){ return 4; } }); } catch(e) {}
"#);
    if strict {
        js.push_str(&r#"
try { Object.defineProperty(navigator, 'userAgent', { get: function(){ return UA_STR; } }); } catch(e) {}
try { Object.defineProperty(navigator, 'platform', { get: function(){ return ''; } }); } catch(e) {}
try { Object.defineProperty(screen, 'colorDepth', { get: function(){ return 24; } }); } catch(e) {}
try { var _mt = TextMetrics; var _mw = CanvasRenderingContext2D.prototype.measureText;
CanvasRenderingContext2D.prototype.measureText = function(t){ var m=_mw.call(this,t); try{ m.width += (krand()-0.5)*0.05; }catch(e){} return m; }; } catch(e) {}
"#
        .replace("UA_STR", &format!("\"Mozilla/5.0 (X11; Linux x86_64) Kestrel/{}.{}\"", env!("CARGO_PKG_VERSION_MAJOR"), env!("CARGO_PKG_VERSION_MINOR"))));
    }
    js.push_str("})();");
    js
}

/// Query-string parameters used for cross-site tracking; stripped from navigations.
const TRACKING_PARAMS: &[&str] = &[
    "utm_source", "utm_medium", "utm_campaign", "utm_term", "utm_content", "utm_id",
    "utm_reader", "utm_social", "utm_name", "utm_cid", "utm_reader", "fbclid", "gclid",
    "dclid", "msclkid", "mc_eid", "mc_cid", "_ga", "_gl", "igshid", "ref_src", "ref_url",
    "yclid", "twclid", "wbraid", "gbraid", "s_kwcid", "li_fat_id", "ttclid",
];

/// Strip known tracking parameters from a URL. Returns the cleaned URL string.
pub fn strip_tracking_params(input: &str) -> String {
    let mut url = match url::Url::parse(input) {
        Ok(u) => u,
        Err(_) => return input.to_string(),
    };
    let dirty = url
        .query_pairs()
        .any(|(k, _)| TRACKING_PARAMS.contains(&k.to_lowercase().as_str()));
    if !dirty {
        return input.to_string();
    }
    let pairs: Vec<(String, String)> = url
        .query_pairs()
        .filter(|(k, _)| !TRACKING_PARAMS.contains(&k.to_lowercase().as_str()))
        .map(|(k, v)| (k.into_owned(), v.into_owned()))
        .collect();
    url.set_query(None);
    if !pairs.is_empty() {
        let qs: String = pairs
            .iter()
            .map(|(k, v)| format!("{k}={v}"))
            .collect::<Vec<_>>()
            .join("&");
        url.set_query(Some(&qs));
    }
    url.to_string()
}

/// HTTPS-first: upgrade plain http:// to https:// for navigations (except localhost).
pub fn https_upgrade(input: &str) -> String {
    if let Ok(mut u) = url::Url::parse(input) {
        let host_ok = u.host_str().map(|h| {
            !(h == "localhost" || h == "127.0.0.1" || h == "::1" || h.ends_with(".local"))
        });
        if u.scheme() == "http" && host_ok.unwrap_or(false) {
            let _ = u.set_scheme("https");
            return u.to_string();
        }
    }
    input.to_string()
}

/// File extensions that trigger a real download instead of in-page navigation.
pub const DOWNLOAD_EXTENSIONS: &[&str] = &[
    "pdf", "zip", "exe", "msi", "dmg", "deb", "rpm", "apk", "iso", "7z", "rar", "gz", "xz",
    "bz2", "tar", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "csv", "epub", "flac", "mp3",
    "ogg", "wav", "mp4", "mkv", "avi", "bin", "torrent", "jar", "patch",
];

pub fn looks_like_download(url: &str) -> bool {
    let Ok(u) = url::Url::parse(url) else { return false };
    let Some(last) = u.path().rsplit('/').next() else { return false };
    let Some((_, ext)) = last.rsplit_once('.') else { return false };
    let ext = ext.to_lowercase();
    ext.len() <= 5 && DOWNLOAD_EXTENSIONS.contains(&ext.as_str())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn strips_utm() {
        let out = strip_tracking_params("https://a.example/x?utm_source=b&id=2");
        assert!(!out.contains("utm_source"));
        assert!(out.contains("id=2"));
    }

    #[test]
    fn keeps_clean_url() {
        assert_eq!(
            strip_tracking_params("https://a.example/x?y=1"),
            "https://a.example/x?y=1"
        );
    }

    #[test]
    fn upgrades_http() {
        assert_eq!(
            https_upgrade("http://example.com/"),
            "https://example.com/"
        );
        assert_eq!(https_upgrade("http://localhost:8080/"), "http://localhost:8080/");
    }

    #[test]
    fn detects_downloads() {
        assert!(looks_like_download("https://example.com/files/setup.exe"));
        assert!(looks_like_download("https://example.com/a/b.PDF"));
        assert!(!looks_like_download("https://example.com/index.html"));
    }

    #[test]
    fn script_has_seed_and_is_level_aware() {
        let off = fingerprint_script(FingerprintLevel::Off, "example.com");
        assert!(off.is_empty());
        let std = fingerprint_script(FingerprintLevel::Standard, "example.com");
        assert!(std.contains("__KSEED"));
        assert!(std.contains("UNMASKED_VENDOR_WEBGL"));
        let strict = fingerprint_script(FingerprintLevel::Strict, "example.com");
        assert!(strict.contains("userAgent"));
    }
}
