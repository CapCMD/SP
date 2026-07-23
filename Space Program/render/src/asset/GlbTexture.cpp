// spr/asset/GlbTexture.cpp
//
// Lecteur GLB autonome + decodage WIC. Voir GlbTexture.hpp pour le contrat
// (fail-safe : aucune exception ne remonte, image vide en cas de probleme).
#include "spr/asset/GlbTexture.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>
#include <utility>

// --- WIC (decodage JPEG/PNG natif Windows, aucune dependance externe) --------
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define INITGUID           // instancie localement CLSID_*/IID_*/GUID_* de WIC
#include <windows.h>
#include <initguid.h>
#include <wincodec.h>

namespace spr::asset {
namespace {

// ============================ mini parseur JSON =============================
// Suffisant pour le glTF (objets/tableaux/chaines/nombres/bool/null). Machine-
// genere et regulier -> un parseur compact et strict fait l'affaire. Toute
// malformation leve une exception, capturee au niveau superieur.
struct JVal {
  enum Type { Null, Bool, Num, Str, Arr, Obj } type{Null};
  bool               b{false};
  double             num{0.0};
  std::string        str;
  std::vector<JVal>  arr;
  std::vector<std::pair<std::string, JVal>> obj;

  const JVal* find(const char* key) const {
    if (type != Obj) return nullptr;
    for (const auto& kv : obj)
      if (kv.first == key) return &kv.second;
    return nullptr;
  }
  double as_num(double def = 0.0) const { return type == Num ? num : def; }
};

class JsonParser {
 public:
  explicit JsonParser(const std::string& s) : s_(s), n_(s.size()) {}
  JVal parse() {
    JVal v = value();
    skip_ws();
    return v;
  }

 private:
  const std::string& s_;
  std::size_t        i_{0};
  std::size_t        n_;

  [[noreturn]] void fail(const char* m) { throw std::runtime_error(std::string("json: ") + m); }
  char peek() { return i_ < n_ ? s_[i_] : '\0'; }
  char get()  { return i_ < n_ ? s_[i_++] : '\0'; }
  void skip_ws() {
    while (i_ < n_) {
      const char c = s_[i_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++i_;
      else break;
    }
  }
  void expect(char c) { if (get() != c) fail("caractere attendu"); }

  JVal value() {
    skip_ws();
    const char c = peek();
    switch (c) {
      case '{': return object();
      case '[': return array();
      case '"': { JVal v; v.type = JVal::Str; v.str = string(); return v; }
      case 't': case 'f': return boolean();
      case 'n': literal("null"); return JVal{};
      default:  return number();
    }
  }
  JVal object() {
    JVal v; v.type = JVal::Obj;
    expect('{'); skip_ws();
    if (peek() == '}') { ++i_; return v; }
    for (;;) {
      skip_ws();
      std::string key = string();
      skip_ws(); expect(':');
      v.obj.emplace_back(std::move(key), value());
      skip_ws();
      const char c = get();
      if (c == ',') continue;
      if (c == '}') break;
      fail("',' ou '}' attendu");
    }
    return v;
  }
  JVal array() {
    JVal v; v.type = JVal::Arr;
    expect('['); skip_ws();
    if (peek() == ']') { ++i_; return v; }
    for (;;) {
      v.arr.push_back(value());
      skip_ws();
      const char c = get();
      if (c == ',') continue;
      if (c == ']') break;
      fail("',' ou ']' attendu");
    }
    return v;
  }
  std::string string() {
    skip_ws(); expect('"');
    std::string out;
    for (;;) {
      char c = get();
      if (c == '\0') fail("chaine non terminee");
      if (c == '"') break;
      if (c == '\\') {
        char e = get();
        switch (e) {
          case '"':  out.push_back('"');  break;
          case '\\': out.push_back('\\'); break;
          case '/':  out.push_back('/');  break;
          case 'n':  out.push_back('\n'); break;
          case 't':  out.push_back('\t'); break;
          case 'r':  out.push_back('\r'); break;
          case 'b':  out.push_back('\b'); break;
          case 'f':  out.push_back('\f'); break;
          case 'u': {
            int cp = 0;
            for (int k = 0; k < 4; ++k) {
              char h = get(); cp <<= 4;
              if (h >= '0' && h <= '9') cp |= (h - '0');
              else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
              else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
              else fail("\\u hex invalide");
            }
            out.push_back(cp < 128 ? static_cast<char>(cp) : '?');   // noms glTF = ASCII
            break;
          }
          default: fail("echappement invalide");
        }
      } else {
        out.push_back(c);
      }
    }
    return out;
  }
  JVal boolean() {
    if (peek() == 't') { literal("true");  JVal v; v.type = JVal::Bool; v.b = true;  return v; }
    literal("false"); JVal v; v.type = JVal::Bool; v.b = false; return v;
  }
  void literal(const char* lit) {
    for (const char* p = lit; *p; ++p)
      if (get() != *p) fail("litteral invalide");
  }
  JVal number() {
    const std::size_t start = i_;
    if (peek() == '-') ++i_;
    while (i_ < n_) {
      const char c = s_[i_];
      if ((c >= '0' && c <= '9') || c == '+' || c == '-' ||
          c == '.' || c == 'e' || c == 'E') ++i_;
      else break;
    }
    if (i_ == start) fail("nombre attendu");
    JVal v; v.type = JVal::Num;
    v.num = std::strtod(s_.c_str() + start, nullptr);
    return v;
  }
};

// ============================ conteneur GLB ================================
constexpr std::uint32_t GLB_MAGIC = 0x46546C67;  // "glTF"
constexpr std::uint32_t CHUNK_JSON = 0x4E4F534A; // "JSON"
constexpr std::uint32_t CHUNK_BIN  = 0x004E4942; // "BIN\0"

std::uint32_t rd_u32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

// ============================ decodage WIC ================================
// Decode data[size] (JPEG/PNG/TIFF...) -> RGBA8. Si max_dim>0 et l'image le
// depasse, downscale proportionnel via IWICBitmapScaler (Fant, haute qualite).
ImageRgba decode_wic(const std::uint8_t* data, std::size_t size, int max_dim) {
  ImageRgba out;
  const HRESULT hrco = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool did_init = SUCCEEDED(hrco);   // S_FALSE si deja initialise -> ne pas Uninit

  IWICImagingFactory*     factory = nullptr;
  IWICStream*             stream  = nullptr;
  IWICBitmapDecoder*      decoder = nullptr;
  IWICBitmapFrameDecode*  frame   = nullptr;
  IWICBitmapScaler*       scaler  = nullptr;
  IWICFormatConverter*    conv    = nullptr;
  do {
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IWICImagingFactory, reinterpret_cast<void**>(&factory))))
      break;
    if (FAILED(factory->CreateStream(&stream))) break;
    if (FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(data),
                                            static_cast<DWORD>(size)))) break;
    if (FAILED(factory->CreateDecoderFromStream(stream, nullptr,
                                                WICDecodeMetadataCacheOnLoad, &decoder))) break;
    if (FAILED(decoder->GetFrame(0, &frame))) break;
    UINT w = 0, h = 0;
    if (FAILED(frame->GetSize(&w, &h)) || w == 0 || h == 0) break;

    IWICBitmapSource* src = frame;   // source du convertisseur (frame ou scaler)
    UINT ow = w, oh = h;
    if (max_dim > 0 && (w > static_cast<UINT>(max_dim) || h > static_cast<UINT>(max_dim))) {
      const double s = static_cast<double>(max_dim) / static_cast<double>(std::max(w, h));
      ow = std::max<UINT>(1, static_cast<UINT>(w * s));
      oh = std::max<UINT>(1, static_cast<UINT>(h * s));
      if (SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
          SUCCEEDED(scaler->Initialize(frame, ow, oh, WICBitmapInterpolationModeFant)))
        src = scaler;
      else { ow = w; oh = h; }   // repli : pleine resolution
    }

    if (FAILED(factory->CreateFormatConverter(&conv))) break;
    if (FAILED(conv->Initialize(src, GUID_WICPixelFormat32bppRGBA,
                                WICBitmapDitherTypeNone, nullptr, 0.0,
                                WICBitmapPaletteTypeCustom))) break;
    const UINT stride = ow * 4;
    std::vector<std::uint8_t> px(static_cast<std::size_t>(stride) * oh);
    if (FAILED(conv->CopyPixels(nullptr, stride,
                                static_cast<UINT>(px.size()), px.data()))) break;
    out.width = static_cast<int>(ow);
    out.height = static_cast<int>(oh);
    out.pixels = std::move(px);
  } while (false);

  if (conv)    conv->Release();
  if (scaler)  scaler->Release();
  if (frame)   frame->Release();
  if (decoder) decoder->Release();
  if (stream)  stream->Release();
  if (factory) factory->Release();
  if (did_init) CoUninitialize();
  return out;
}

// ===================== geometrie GLB (maillage) ===========================
// Petite algebre 4x4 DOUBLE, COLONNE-MAJEURE (m[col*4+row]) : meme convention que
// glTF ("matrix") -> transforms de node accumulees sans dependre de spr::Math
// (l'asset reste autonome, cf. contrat GlbTexture.hpp).
struct M4 {
  double m[16];
  static M4 id() { M4 r{}; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0; return r; }
  M4 operator*(const M4& o) const {
    M4 r{};
    for (int c = 0; c < 4; ++c)
      for (int row = 0; row < 4; ++row) {
        double s = 0.0;
        for (int k = 0; k < 4; ++k) s += m[k * 4 + row] * o.m[c * 4 + k];
        r.m[c * 4 + row] = s;
      }
    return r;
  }
  void point(float x, float y, float z, float& ox, float& oy, float& oz) const {
    ox = static_cast<float>(m[0] * x + m[4] * y + m[8]  * z + m[12]);
    oy = static_cast<float>(m[1] * x + m[5] * y + m[9]  * z + m[13]);
    oz = static_cast<float>(m[2] * x + m[6] * y + m[10] * z + m[14]);
  }
  void dir(float x, float y, float z, float& ox, float& oy, float& oz) const {
    ox = static_cast<float>(m[0] * x + m[4] * y + m[8]  * z);
    oy = static_cast<float>(m[1] * x + m[5] * y + m[9]  * z);
    oz = static_cast<float>(m[2] * x + m[6] * y + m[10] * z);
  }
};

// Petits accesseurs JSON surs (indices/valeurs par defaut).
const JVal* arr_at(const JVal* a, int i) {
  if (!a || a->type != JVal::Arr || i < 0 || i >= static_cast<int>(a->arr.size())) return nullptr;
  return &a->arr[static_cast<std::size_t>(i)];
}
int jint(const JVal* o, const char* k, int def) {
  if (!o) return def;
  const JVal* v = o->find(k);
  return (v && v->type == JVal::Num) ? static_cast<int>(v->num) : def;
}

// Transform locale d'un node : "matrix" explicite sinon T * R(quat) * S.
M4 trs_matrix(const JVal& node) {
  if (const JVal* mm = node.find("matrix"); mm && mm->type == JVal::Arr && mm->arr.size() == 16) {
    M4 r{}; for (int i = 0; i < 16; ++i) r.m[i] = mm->arr[static_cast<std::size_t>(i)].as_num();
    return r;
  }
  double t[3] = {0, 0, 0}, q[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};
  if (const JVal* v = node.find("translation"); v && v->type == JVal::Arr && v->arr.size() == 3)
    for (int i = 0; i < 3; ++i) t[i] = v->arr[static_cast<std::size_t>(i)].as_num();
  if (const JVal* v = node.find("rotation"); v && v->type == JVal::Arr && v->arr.size() == 4)
    for (int i = 0; i < 4; ++i) q[i] = v->arr[static_cast<std::size_t>(i)].as_num();
  if (const JVal* v = node.find("scale"); v && v->type == JVal::Arr && v->arr.size() == 3)
    for (int i = 0; i < 3; ++i) s[i] = v->arr[static_cast<std::size_t>(i)].as_num();
  const double qn = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
  if (qn > 1e-12) { for (double& c : q) c /= qn; } else { q[0] = q[1] = q[2] = 0; q[3] = 1; }
  const double x = q[0], y = q[1], z = q[2], w = q[3];
  // R * S (colonne-majeure : m[col*4+row])
  M4 r = M4::id();
  r.m[0] = (1 - 2 * (y * y + z * z)) * s[0]; r.m[1] = (2 * (x * y + w * z)) * s[0]; r.m[2]  = (2 * (x * z - w * y)) * s[0];
  r.m[4] = (2 * (x * y - w * z)) * s[1];     r.m[5] = (1 - 2 * (x * x + z * z)) * s[1]; r.m[6]  = (2 * (y * z + w * x)) * s[1];
  r.m[8] = (2 * (x * z + w * y)) * s[2];     r.m[9] = (2 * (y * z - w * x)) * s[2];     r.m[10] = (1 - 2 * (x * x + y * y)) * s[2];
  r.m[12] = t[0]; r.m[13] = t[1]; r.m[14] = t[2];
  return r;
}

// Lit un accessor float VEC3 -> APPEND (count*3) dans `out`. Renvoie count, ou -1.
long read_vec3(const JVal* accessors, const JVal* views, const std::uint8_t* bin,
               std::size_t bin_len, int acc_index, std::vector<float>& out) {
  const JVal* acc = arr_at(accessors, acc_index);
  if (!acc) return -1;
  if (jint(acc, "componentType", 0) != 5126) return -1;   // FLOAT
  const JVal* ty = acc->find("type");
  if (!ty || ty->type != JVal::Str || ty->str != "VEC3") return -1;
  const int count   = jint(acc, "count", 0);
  const JVal* bv    = arr_at(views, jint(acc, "bufferView", -1));
  if (count <= 0 || !bv) return -1;
  const std::size_t bvOff  = static_cast<std::size_t>(jint(bv, "byteOffset", 0));
  const std::size_t accOff = static_cast<std::size_t>(jint(acc, "byteOffset", 0));
  std::size_t stride = static_cast<std::size_t>(jint(bv, "byteStride", 0));
  if (stride == 0) stride = 12;
  const std::size_t base = bvOff + accOff;
  if (base + static_cast<std::size_t>(count - 1) * stride + 12 > bin_len) return -1;
  const std::size_t start = out.size();
  out.resize(start + static_cast<std::size_t>(count) * 3);
  for (int i = 0; i < count; ++i) {
    float f[3];
    std::memcpy(f, bin + base + static_cast<std::size_t>(i) * stride, 12);
    out[start + static_cast<std::size_t>(i) * 3 + 0] = f[0];
    out[start + static_cast<std::size_t>(i) * 3 + 1] = f[1];
    out[start + static_cast<std::size_t>(i) * 3 + 2] = f[2];
  }
  return count;
}

// Lit un accessor float VEC2 -> APPEND (count*2) dans `out`. Renvoie count, ou -1.
// (UV de texture : TEXCOORD_0. Seul le type FLOAT est gere ; les UV normalisees
// u8/u16 -> repli sur (0,0) cote appelant, sans casser le chargement.)
long read_vec2(const JVal* accessors, const JVal* views, const std::uint8_t* bin,
               std::size_t bin_len, int acc_index, std::vector<float>& out) {
  const JVal* acc = arr_at(accessors, acc_index);
  if (!acc) return -1;
  if (jint(acc, "componentType", 0) != 5126) return -1;   // FLOAT
  const JVal* ty = acc->find("type");
  if (!ty || ty->type != JVal::Str || ty->str != "VEC2") return -1;
  const int count   = jint(acc, "count", 0);
  const JVal* bv    = arr_at(views, jint(acc, "bufferView", -1));
  if (count <= 0 || !bv) return -1;
  const std::size_t bvOff  = static_cast<std::size_t>(jint(bv, "byteOffset", 0));
  const std::size_t accOff = static_cast<std::size_t>(jint(acc, "byteOffset", 0));
  std::size_t stride = static_cast<std::size_t>(jint(bv, "byteStride", 0));
  if (stride == 0) stride = 8;
  const std::size_t base = bvOff + accOff;
  if (base + static_cast<std::size_t>(count - 1) * stride + 8 > bin_len) return -1;
  const std::size_t start = out.size();
  out.resize(start + static_cast<std::size_t>(count) * 2);
  for (int i = 0; i < count; ++i) {
    float f[2];
    std::memcpy(f, bin + base + static_cast<std::size_t>(i) * stride, 8);
    out[start + static_cast<std::size_t>(i) * 2 + 0] = f[0];
    out[start + static_cast<std::size_t>(i) * 2 + 1] = f[1];
  }
  return count;
}

// Lit un accessor d'indices SCALAR (u8/u16/u32) -> APPEND (v+base_vertex). false si echec.
bool read_indices(const JVal* accessors, const JVal* views, const std::uint8_t* bin,
                  std::size_t bin_len, int acc_index, std::uint32_t base_vertex,
                  std::vector<std::uint32_t>& out) {
  const JVal* acc = arr_at(accessors, acc_index);
  if (!acc) return false;
  const int ct    = jint(acc, "componentType", 0);
  const int count = jint(acc, "count", 0);
  const JVal* bv  = arr_at(views, jint(acc, "bufferView", -1));
  if (count <= 0 || !bv) return false;
  const std::size_t comp = (ct == 5121) ? 1 : (ct == 5123) ? 2 : (ct == 5125) ? 4 : 0;
  if (comp == 0) return false;
  const std::size_t bvOff  = static_cast<std::size_t>(jint(bv, "byteOffset", 0));
  const std::size_t accOff = static_cast<std::size_t>(jint(acc, "byteOffset", 0));
  std::size_t stride = static_cast<std::size_t>(jint(bv, "byteStride", 0));
  if (stride == 0) stride = comp;
  const std::size_t base = bvOff + accOff;
  if (base + static_cast<std::size_t>(count - 1) * stride + comp > bin_len) return false;
  out.reserve(out.size() + static_cast<std::size_t>(count));
  for (int i = 0; i < count; ++i) {
    const std::uint8_t* p = bin + base + static_cast<std::size_t>(i) * stride;
    std::uint32_t v = 0;
    if (comp == 1) v = p[0];
    else if (comp == 2) { std::uint16_t t; std::memcpy(&t, p, 2); v = t; }
    else { std::uint32_t t; std::memcpy(&t, p, 4); v = t; }
    out.push_back(base_vertex + v);
  }
  return true;
}

}  // namespace

ImageRgba load_image_file(const std::string& path, int max_dim) {
  try {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const std::streamoff sz = f.tellg();
    if (sz <= 0) return {};
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(sz));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    return decode_wic(buf.data(), buf.size(), max_dim);
  } catch (...) {
    return {};
  }
}

ImageRgba load_glb_image_by_name(const std::string& glb_path, const std::string& name_substr,
                                 int max_dim) {
  try {
    std::ifstream f(glb_path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const std::streamoff sz = f.tellg();
    if (sz < 20) return {};
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(sz));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), sz);

    if (rd_u32(&buf[0]) != GLB_MAGIC) return {};   // pas un GLB
    // buf[4]=version, buf[8]=length total. Chunks a partir de l'offset 12.
    std::size_t off = 12;
    std::string json;
    const std::uint8_t* bin = nullptr;
    std::size_t bin_len = 0;
    while (off + 8 <= buf.size()) {
      const std::uint32_t clen = rd_u32(&buf[off]);
      const std::uint32_t ctype = rd_u32(&buf[off + 4]);
      const std::size_t   cdata = off + 8;
      if (cdata + clen > buf.size()) break;
      if (ctype == CHUNK_JSON) {
        json.assign(reinterpret_cast<const char*>(&buf[cdata]), clen);
      } else if (ctype == CHUNK_BIN) {
        bin = &buf[cdata];
        bin_len = clen;
      }
      off = cdata + clen + ((clen % 4) ? (4 - clen % 4) : 0);  // padding 4 octets
    }
    if (json.empty() || !bin) return {};

    JVal root = JsonParser(json).parse();
    const JVal* images = root.find("images");
    const JVal* views  = root.find("bufferViews");
    if (!images || images->type != JVal::Arr || !views || views->type != JVal::Arr) return {};

    // Trouve l'image dont le nom contient name_substr (insensible a la casse).
    auto lower = [](std::string s) {
      for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      return s;
    };
    const std::string needle = lower(name_substr);
    int bv_index = -1;
    for (const JVal& img : images->arr) {
      const JVal* nm = img.find("name");
      if (!nm || nm->type != JVal::Str) continue;
      if (lower(nm->str).find(needle) == std::string::npos) continue;
      const JVal* bv = img.find("bufferView");
      if (bv && bv->type == JVal::Num) bv_index = static_cast<int>(bv->num);
      break;
    }
    if (bv_index < 0 || bv_index >= static_cast<int>(views->arr.size())) return {};

    const JVal& bv = views->arr[static_cast<std::size_t>(bv_index)];
    const std::size_t byte_off = static_cast<std::size_t>(bv.find("byteOffset") ?
                                     bv.find("byteOffset")->as_num() : 0.0);
    const JVal* blen = bv.find("byteLength");
    if (!blen) return {};
    const std::size_t byte_len = static_cast<std::size_t>(blen->as_num());
    if (byte_len == 0 || byte_off + byte_len > bin_len) return {};

    return decode_wic(bin + byte_off, byte_len, max_dim);
  } catch (...) {
    return {};   // fail-safe absolu
  }
}

MeshData load_glb_mesh(const std::string& glb_path) {
  try {
    std::ifstream f(glb_path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const std::streamoff sz = f.tellg();
    if (sz < 20) return {};
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(sz));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    if (rd_u32(&buf[0]) != GLB_MAGIC) return {};

    // decoupe conteneur : chunk JSON + chunk BIN
    std::string json;
    const std::uint8_t* bin = nullptr;
    std::size_t bin_len = 0;
    std::size_t off = 12;
    while (off + 8 <= buf.size()) {
      const std::uint32_t clen  = rd_u32(&buf[off]);
      const std::uint32_t ctype = rd_u32(&buf[off + 4]);
      const std::size_t   cdata = off + 8;
      if (cdata + clen > buf.size()) break;
      if (ctype == CHUNK_JSON) json.assign(reinterpret_cast<const char*>(&buf[cdata]), clen);
      else if (ctype == CHUNK_BIN) { bin = &buf[cdata]; bin_len = clen; }
      off = cdata + clen + ((clen % 4) ? (4 - clen % 4) : 0);
    }
    if (json.empty() || !bin) return {};

    JVal root = JsonParser(json).parse();
    // compression (Draco/meshopt) ou toute extension requise -> abandon fail-safe.
    if (const JVal* er = root.find("extensionsRequired");
        er && er->type == JVal::Arr && !er->arr.empty())
      return {};
    const JVal* accessors = root.find("accessors");
    const JVal* views     = root.find("bufferViews");
    const JVal* meshes    = root.find("meshes");
    const JVal* nodes     = root.find("nodes");
    if (!accessors || accessors->type != JVal::Arr || !views || views->type != JVal::Arr ||
        !meshes || meshes->type != JVal::Arr || !nodes || nodes->type != JVal::Arr)
      return {};

    // racines de la scene par defaut (sinon, tous les nodes).
    std::vector<int> roots;
    const JVal* scenes = root.find("scenes");
    const JVal* sc = root.find("scene");
    const JVal* scene = arr_at(scenes, (sc && sc->type == JVal::Num) ? static_cast<int>(sc->num) : 0);
    if (scene) {
      const JVal* sn = scene->find("nodes");
      if (sn && sn->type == JVal::Arr)
        for (const JVal& e : sn->arr) if (e.type == JVal::Num) roots.push_back(static_cast<int>(e.num));
    }
    if (roots.empty())
      for (int i = 0; i < static_cast<int>(nodes->arr.size()); ++i) roots.push_back(i);

    // parcours de la hierarchie (pile explicite : node + transform monde).
    MeshData out;
    struct Item { int node; M4 world; };
    std::vector<Item> stack;
    for (int r : roots) stack.push_back({r, M4::id()});
    std::size_t guard = 0;
    while (!stack.empty()) {
      if (++guard > 2000000u) break;   // garde-fou (hierarchie pathologique)
      const Item it = stack.back(); stack.pop_back();
      const JVal* node = arr_at(nodes, it.node);
      if (!node) continue;
      const M4 world = it.world * trs_matrix(*node);

      const JVal* mi = node->find("mesh");
      if (mi && mi->type == JVal::Num) {
        const JVal* mesh  = arr_at(meshes, static_cast<int>(mi->num));
        const JVal* prims = mesh ? mesh->find("primitives") : nullptr;
        if (prims && prims->type == JVal::Arr) {
          for (const JVal& prim : prims->arr) {
            const JVal* mode = prim.find("mode");
            if (mode && mode->type == JVal::Num && static_cast<int>(mode->num) != 4) continue;  // TRIANGLES seul
            const JVal* attr = prim.find("attributes");
            if (!attr) continue;
            const JVal* pos = attr->find("POSITION");
            const JVal* idx = prim.find("indices");
            if (!pos || pos->type != JVal::Num || !idx || idx->type != JVal::Num) continue;

            std::vector<float> pv;
            const long nverts = read_vec3(accessors, views, bin, bin_len, static_cast<int>(pos->num), pv);
            if (nverts <= 0) continue;
            std::vector<std::uint32_t> pidx;
            if (!read_indices(accessors, views, bin, bin_len, static_cast<int>(idx->num), 0, pidx)) continue;

            std::vector<float> nv;
            const JVal* nrm = attr->find("NORMAL");
            const bool have_n = nrm && nrm->type == JVal::Num &&
                                read_vec3(accessors, views, bin, bin_len, static_cast<int>(nrm->num), nv) == nverts;

            const std::uint32_t base = static_cast<std::uint32_t>(out.positions.size() / 3);
            const std::size_t vstart = out.positions.size();
            out.positions.resize(vstart + pv.size());
            for (long i = 0; i < nverts; ++i)
              world.point(pv[i * 3], pv[i * 3 + 1], pv[i * 3 + 2],
                          out.positions[vstart + i * 3], out.positions[vstart + i * 3 + 1],
                          out.positions[vstart + i * 3 + 2]);

            out.normals.resize(vstart + pv.size(), 0.0f);
            if (have_n)
              for (long i = 0; i < nverts; ++i) {
                float ox, oy, oz;
                world.dir(nv[i * 3], nv[i * 3 + 1], nv[i * 3 + 2], ox, oy, oz);
                const float ln = std::sqrt(ox * ox + oy * oy + oz * oz);
                const float s = (ln > 1e-8f) ? 1.0f / ln : 0.0f;
                out.normals[vstart + i * 3] = ox * s;
                out.normals[vstart + i * 3 + 1] = oy * s;
                out.normals[vstart + i * 3 + 2] = oz * s;
              }

            for (std::uint32_t v : pidx) out.indices.push_back(base + v);

            if (!have_n)   // normales de facette accumulees (lissage doux)
              for (std::size_t t = 0; t + 2 < pidx.size(); t += 3) {
                const std::uint32_t ia = base + pidx[t], ib = base + pidx[t + 1], ic = base + pidx[t + 2];
                const float* A = &out.positions[ia * 3];
                const float* B = &out.positions[ib * 3];
                const float* C = &out.positions[ic * 3];
                const float ux = B[0] - A[0], uy = B[1] - A[1], uz = B[2] - A[2];
                const float vx = C[0] - A[0], vy = C[1] - A[1], vz = C[2] - A[2];
                const float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
                for (std::uint32_t id2 : {ia, ib, ic}) {
                  out.normals[id2 * 3] += nx; out.normals[id2 * 3 + 1] += ny; out.normals[id2 * 3 + 2] += nz;
                }
              }
          }
        }
      }

      const JVal* ch = node->find("children");
      if (ch && ch->type == JVal::Arr)
        for (const JVal& e : ch->arr)
          if (e.type == JVal::Num) stack.push_back({static_cast<int>(e.num), world});
    }

    if (out.positions.empty() || out.indices.empty()) return {};

    // renormalise (les normales de facette accumulees ne sont pas unitaires).
    for (std::size_t i = 0; i + 3 <= out.normals.size(); i += 3) {
      const float nx = out.normals[i], ny = out.normals[i + 1], nz = out.normals[i + 2];
      const float ln = std::sqrt(nx * nx + ny * ny + nz * nz);
      if (ln > 1e-8f) { out.normals[i] = nx / ln; out.normals[i + 1] = ny / ln; out.normals[i + 2] = nz / ln; }
      else { out.normals[i] = 0; out.normals[i + 1] = 0; out.normals[i + 2] = 1; }
    }

    // boite englobante (unites du modele).
    out.min[0] = out.max[0] = out.positions[0];
    out.min[1] = out.max[1] = out.positions[1];
    out.min[2] = out.max[2] = out.positions[2];
    for (std::size_t i = 0; i + 2 < out.positions.size(); i += 3)
      for (int k = 0; k < 3; ++k) {
        const float c = out.positions[i + k];
        if (c < out.min[k]) out.min[k] = c;
        if (c > out.max[k]) out.max[k] = c;
      }
    return out;
  } catch (...) {
    return {};   // fail-safe absolu
  }
}

GlbModel load_glb_model(const std::string& glb_path, int max_tex_dim) {
  try {
    std::ifstream f(glb_path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const std::streamoff sz = f.tellg();
    if (sz < 20) return {};
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(sz));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    if (rd_u32(&buf[0]) != GLB_MAGIC) return {};

    std::string json;
    const std::uint8_t* bin = nullptr;
    std::size_t bin_len = 0;
    std::size_t off = 12;
    while (off + 8 <= buf.size()) {
      const std::uint32_t clen  = rd_u32(&buf[off]);
      const std::uint32_t ctype = rd_u32(&buf[off + 4]);
      const std::size_t   cdata = off + 8;
      if (cdata + clen > buf.size()) break;
      if (ctype == CHUNK_JSON) json.assign(reinterpret_cast<const char*>(&buf[cdata]), clen);
      else if (ctype == CHUNK_BIN) { bin = &buf[cdata]; bin_len = clen; }
      off = cdata + clen + ((clen % 4) ? (4 - clen % 4) : 0);
    }
    if (json.empty() || !bin) return {};

    JVal root = JsonParser(json).parse();
    if (const JVal* er = root.find("extensionsRequired");
        er && er->type == JVal::Arr && !er->arr.empty())
      return {};   // Draco/meshopt/... non gere -> repli fail-safe
    const JVal* accessors = root.find("accessors");
    const JVal* views     = root.find("bufferViews");
    const JVal* meshes    = root.find("meshes");
    const JVal* nodes     = root.find("nodes");
    if (!accessors || accessors->type != JVal::Arr || !views || views->type != JVal::Arr ||
        !meshes || meshes->type != JVal::Arr || !nodes || nodes->type != JVal::Arr)
      return {};
    const JVal* materials = root.find("materials");
    const JVal* textures  = root.find("textures");
    const JVal* images    = root.find("images");

    GlbModel model;

    // Decodage d'une image glTF (par index) -> index local dans model.images
    // (dedoublonne : plusieurs materiaux peuvent partager la meme carte). `linear` =
    // carte de donnee (normales/occlusion) a NE PAS traiter en sRGB ; le premier
    // role rencontre fixe l'espace (aucun conflit sur nos modeles : roles disjoints).
    std::map<int, int> img_local;
    auto decode_img = [&](int gltf_img, bool linear) -> int {
      if (gltf_img < 0) return -1;
      auto it = img_local.find(gltf_img);
      if (it != img_local.end()) return it->second;
      int local = -1;
      const JVal* im = arr_at(images, gltf_img);
      if (im) {
        const JVal* bvj = im->find("bufferView");
        if (bvj && bvj->type == JVal::Num) {
          const JVal* bv = arr_at(views, static_cast<int>(bvj->num));
          if (bv) {
            const std::size_t ioff = static_cast<std::size_t>(jint(bv, "byteOffset", 0));
            const JVal* bl = bv->find("byteLength");
            if (bl) {
              const std::size_t ilen = static_cast<std::size_t>(bl->as_num());
              if (ilen > 0 && ioff + ilen <= bin_len) {
                ImageRgba dec = decode_wic(bin + ioff, ilen, max_tex_dim);
                if (dec.ok()) {
                  local = static_cast<int>(model.images.size());
                  model.images.push_back(std::move(dec));
                  model.image_linear.push_back(linear ? 1 : 0);
                }
              }
            }
          }
        }
      }
      img_local[gltf_img] = local;
      return local;
    };
    auto tex_source = [&](const JVal* tex_ref) -> int {   // {index:tex}.source -> image
      if (!tex_ref) return -1;
      const JVal* tx = arr_at(textures, jint(tex_ref, "index", -1));
      return tx ? jint(tx, "source", -1) : -1;
    };

    // Resout un materiau -> cartes baseColor / normales / occlusion + facteurs
    // (baseColor + EMISSIF pour les neons/ecrans auto-eclaires).
    auto resolve_material = [&](int mat_idx, int& out_img, int& out_nrm, int& out_ao,
                                float bc[4], float emis[3], float& emis_str) {
      out_img = out_nrm = out_ao = -1;
      bc[0] = bc[1] = bc[2] = bc[3] = 1.0f;
      emis[0] = emis[1] = emis[2] = 0.0f; emis_str = 1.0f;
      const JVal* m = arr_at(materials, mat_idx);
      if (!m) return;
      const JVal* pmr = m->find("pbrMetallicRoughness");
      if (pmr) {
        if (const JVal* bcf = pmr->find("baseColorFactor"); bcf && bcf->type == JVal::Arr)
          for (int k = 0; k < 4 && k < static_cast<int>(bcf->arr.size()); ++k)
            bc[k] = static_cast<float>(bcf->arr[static_cast<std::size_t>(k)].as_num(1.0));
        if (const JVal* bct = pmr->find("baseColorTexture"))
          out_img = decode_img(tex_source(bct), /*linear=*/false);   // couleur = sRGB
      }
      if (const JVal* nt = m->find("normalTexture"))
        out_nrm = decode_img(tex_source(nt), /*linear=*/true);       // normales = lineaire
      if (const JVal* ot = m->find("occlusionTexture"))
        out_ao  = decode_img(tex_source(ot), /*linear=*/true);       // occlusion = lineaire
      // EMISSIF : emissiveFactor + intensite (extension KHR_materials_emissive_strength).
      if (const JVal* ef = m->find("emissiveFactor"); ef && ef->type == JVal::Arr)
        for (int k = 0; k < 3 && k < static_cast<int>(ef->arr.size()); ++k)
          emis[k] = static_cast<float>(ef->arr[static_cast<std::size_t>(k)].as_num(0.0));
      if (const JVal* ext = m->find("extensions"))
        if (const JVal* ke = ext->find("KHR_materials_emissive_strength"))
          if (const JVal* es = ke->find("emissiveStrength"))
            emis_str = static_cast<float>(es->as_num(1.0));
      // neon defini SEULEMENT par une carte emissive (facteur nul) -> glow blanc.
      if (m->find("emissiveTexture") && (emis[0] + emis[1] + emis[2] < 0.01f))
        emis[0] = emis[1] = emis[2] = 1.0f;
    };

    // groupement par materiau : index materiau glTF -> index de sous-maillage.
    std::map<int, int> mat_to_sub;

    // racines de la scene par defaut (sinon tous les nodes).
    std::vector<int> roots;
    const JVal* scenes = root.find("scenes");
    const JVal* sc = root.find("scene");
    const JVal* scene = arr_at(scenes, (sc && sc->type == JVal::Num) ? static_cast<int>(sc->num) : 0);
    if (scene) {
      const JVal* sn = scene->find("nodes");
      if (sn && sn->type == JVal::Arr)
        for (const JVal& e : sn->arr) if (e.type == JVal::Num) roots.push_back(static_cast<int>(e.num));
    }
    if (roots.empty())
      for (int i = 0; i < static_cast<int>(nodes->arr.size()); ++i) roots.push_back(i);

    struct Item { int node; M4 world; };
    std::vector<Item> stack;
    for (int r : roots) stack.push_back({r, M4::id()});
    std::size_t guard = 0;
    while (!stack.empty()) {
      if (++guard > 2000000u) break;
      const Item it = stack.back(); stack.pop_back();
      const JVal* node = arr_at(nodes, it.node);
      if (!node) continue;
      const M4 world = it.world * trs_matrix(*node);

      const JVal* mi = node->find("mesh");
      if (mi && mi->type == JVal::Num) {
        const JVal* mesh  = arr_at(meshes, static_cast<int>(mi->num));
        const JVal* prims = mesh ? mesh->find("primitives") : nullptr;
        if (prims && prims->type == JVal::Arr) {
          for (const JVal& prim : prims->arr) {
            const JVal* mode = prim.find("mode");
            if (mode && mode->type == JVal::Num && static_cast<int>(mode->num) != 4) continue;
            const JVal* attr = prim.find("attributes");
            if (!attr) continue;
            const JVal* pos = attr->find("POSITION");
            const JVal* idx = prim.find("indices");
            if (!pos || pos->type != JVal::Num || !idx || idx->type != JVal::Num) continue;

            std::vector<float> pv;
            const long nverts = read_vec3(accessors, views, bin, bin_len, static_cast<int>(pos->num), pv);
            if (nverts <= 0) continue;
            std::vector<std::uint32_t> pidx;
            if (!read_indices(accessors, views, bin, bin_len, static_cast<int>(idx->num), 0, pidx)) continue;

            std::vector<float> nv;
            const JVal* nrm = attr->find("NORMAL");
            const bool have_n = nrm && nrm->type == JVal::Num &&
                                read_vec3(accessors, views, bin, bin_len, static_cast<int>(nrm->num), nv) == nverts;
            std::vector<float> uvv;
            const JVal* uva = attr->find("TEXCOORD_0");
            const bool have_uv = uva && uva->type == JVal::Num &&
                                 read_vec2(accessors, views, bin, bin_len, static_cast<int>(uva->num), uvv) == nverts;

            const int mat_idx = jint(&prim, "material", -1);
            int subi;
            auto fs = mat_to_sub.find(mat_idx);
            if (fs == mat_to_sub.end()) {
              subi = static_cast<int>(model.submeshes.size());
              model.submeshes.emplace_back();
              int img, nrm, ao; float bc[4], emis[3], emis_str;
              resolve_material(mat_idx, img, nrm, ao, bc, emis, emis_str);
              model.submeshes[subi].image_index  = img;
              model.submeshes[subi].normal_index = nrm;
              model.submeshes[subi].ao_index     = ao;
              std::memcpy(model.submeshes[subi].base_color, bc, sizeof(float) * 4);
              std::memcpy(model.submeshes[subi].emissive, emis, sizeof(float) * 3);
              model.submeshes[subi].emissive_strength = emis_str;
              mat_to_sub[mat_idx] = subi;
            } else {
              subi = fs->second;
            }
            GlbSubMesh& S = model.submeshes[static_cast<std::size_t>(subi)];

            const std::uint32_t base = static_cast<std::uint32_t>(S.positions.size() / 3);
            const std::size_t vstart = S.positions.size();
            S.positions.resize(vstart + pv.size());
            for (long i = 0; i < nverts; ++i)
              world.point(pv[i * 3], pv[i * 3 + 1], pv[i * 3 + 2],
                          S.positions[vstart + i * 3], S.positions[vstart + i * 3 + 1],
                          S.positions[vstart + i * 3 + 2]);

            S.normals.resize(vstart + pv.size(), 0.0f);
            if (have_n)
              for (long i = 0; i < nverts; ++i) {
                float ox, oy, oz;
                world.dir(nv[i * 3], nv[i * 3 + 1], nv[i * 3 + 2], ox, oy, oz);
                const float ln = std::sqrt(ox * ox + oy * oy + oz * oz);
                const float s = (ln > 1e-8f) ? 1.0f / ln : 0.0f;
                S.normals[vstart + i * 3] = ox * s;
                S.normals[vstart + i * 3 + 1] = oy * s;
                S.normals[vstart + i * 3 + 2] = oz * s;
              }

            const std::size_t uvstart = S.uvs.size();
            S.uvs.resize(uvstart + static_cast<std::size_t>(nverts) * 2, 0.0f);
            if (have_uv)
              for (long i = 0; i < nverts; ++i) {
                S.uvs[uvstart + i * 2]     = uvv[i * 2];
                S.uvs[uvstart + i * 2 + 1] = uvv[i * 2 + 1];
              }

            for (std::uint32_t v : pidx) S.indices.push_back(base + v);

            if (!have_n)   // normales de facette accumulees (renormalisees en fin)
              for (std::size_t t = 0; t + 2 < pidx.size(); t += 3) {
                const std::uint32_t ia = base + pidx[t], ib = base + pidx[t + 1], ic = base + pidx[t + 2];
                const float* A = &S.positions[ia * 3];
                const float* B = &S.positions[ib * 3];
                const float* C = &S.positions[ic * 3];
                const float ux = B[0] - A[0], uy = B[1] - A[1], uz = B[2] - A[2];
                const float vx = C[0] - A[0], vy = C[1] - A[1], vz = C[2] - A[2];
                const float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
                for (std::uint32_t id2 : {ia, ib, ic}) {
                  S.normals[id2 * 3] += nx; S.normals[id2 * 3 + 1] += ny; S.normals[id2 * 3 + 2] += nz;
                }
              }
          }
        }
      }

      const JVal* ch = node->find("children");
      if (ch && ch->type == JVal::Arr)
        for (const JVal& e : ch->arr)
          if (e.type == JVal::Num) stack.push_back({static_cast<int>(e.num), world});
    }

    // purge des sous-maillages vides, renormalisation, boite englobante globale.
    model.submeshes.erase(
        std::remove_if(model.submeshes.begin(), model.submeshes.end(),
                       [](const GlbSubMesh& s) { return s.positions.empty() || s.indices.empty(); }),
        model.submeshes.end());
    if (model.submeshes.empty()) return {};

    bool bbinit = false;
    for (GlbSubMesh& S : model.submeshes) {
      for (std::size_t i = 0; i + 3 <= S.normals.size(); i += 3) {
        const float nx = S.normals[i], ny = S.normals[i + 1], nz = S.normals[i + 2];
        const float ln = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (ln > 1e-8f) { S.normals[i] = nx / ln; S.normals[i + 1] = ny / ln; S.normals[i + 2] = nz / ln; }
        else { S.normals[i] = 0; S.normals[i + 1] = 0; S.normals[i + 2] = 1; }
      }
      for (std::size_t i = 0; i + 2 < S.positions.size(); i += 3) {
        for (int k = 0; k < 3; ++k) {
          const float c = S.positions[i + k];
          if (!bbinit) { model.min[k] = model.max[k] = c; }
          else { if (c < model.min[k]) model.min[k] = c; if (c > model.max[k]) model.max[k] = c; }
        }
        bbinit = true;   // amorce faite apres le PREMIER sommet (les 3 axes)
      }
    }
    return model;
  } catch (...) {
    return {};   // fail-safe absolu
  }
}

} // namespace spr::asset
