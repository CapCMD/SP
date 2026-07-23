// fen/save/Save.hpp — sérialisation versionnée + hash d'état [M7, GDD 18]
//
// L'UNE DES TROIS DETTES À NE JAMAIS DIFFÉRER [carte P4] : l'INTERFACE est
// figée ici ; chaque système l'implémente incrémentalement.
// CONTRAT DE DÉTERMINISME : même graine + mêmes entrées -> même état, donc
//   save -> load -> save  DOIT être BYTE-IDENTIQUE (test-clé en CI).
// Archive binaire little-endian versionnée ; les doubles sont écrits par
// memcpy de leur représentation IEEE754 : aucun formatage texte, aucun arrondi.
// Solo 100 % hors-ligne [GDD 18] : aucun serveur dans la boucle.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace fen::save {

inline constexpr std::uint32_t SAVE_MAGIC = 0x53455241u; // "ARES"
inline constexpr std::uint32_t SCHEMA_VERSION = 1;

// --- Écriture ----------------------------------------------------------------
class Writer {
 public:
  Writer() { u32(SAVE_MAGIC); u32(SCHEMA_VERSION); }

  void u8(std::uint8_t v)   { buf_.push_back(v); }
  void u32(std::uint32_t v) { raw(&v, 4); }
  void u64(std::uint64_t v) { raw(&v, 8); }
  void i32(std::int32_t v)  { raw(&v, 4); }
  void f64(double v)        { raw(&v, 8); }   // bits IEEE754, jamais de texte
  void boolean(bool v)      { u8(v ? 1 : 0); }
  void str(const std::string& s) {
    u32(static_cast<std::uint32_t>(s.size()));
    raw(s.data(), s.size());
  }
  template <typename T, typename Fn>
  void vec(const std::vector<T>& v, Fn writeOne) {
    u32(static_cast<std::uint32_t>(v.size()));
    for (const auto& x : v) writeOne(*this, x);
  }
  const std::vector<std::uint8_t>& bytes() const { return buf_; }

 private:
  void raw(const void* p, std::size_t n) {
    const auto* b = static_cast<const std::uint8_t*>(p);
    buf_.insert(buf_.end(), b, b + n);
  }
  std::vector<std::uint8_t> buf_;
};

// --- Lecture -----------------------------------------------------------------
class Reader {
 public:
  Reader(const std::uint8_t* data, std::size_t size) : p_(data), end_(data + size) {
    magic_ok_ = (size >= 8) && (peek_u32(0) == SAVE_MAGIC);
    version_ = magic_ok_ ? peek_u32(4) : 0;
    p_ += 8;
  }
  bool ok() const { return magic_ok_ && !failed_; }
  std::uint32_t version() const { return version_; }

  std::uint8_t u8()    { std::uint8_t v{};  raw(&v, 1); return v; }
  std::uint32_t u32()  { std::uint32_t v{}; raw(&v, 4); return v; }
  std::uint64_t u64()  { std::uint64_t v{}; raw(&v, 8); return v; }
  std::int32_t i32()   { std::int32_t v{};  raw(&v, 4); return v; }
  double f64()         { double v{};        raw(&v, 8); return v; }
  bool boolean()       { return u8() != 0; }
  std::string str() {
    const std::uint32_t n = u32();
    if (p_ + n > end_) { failed_ = true; return {}; }
    std::string s(reinterpret_cast<const char*>(p_), n);
    p_ += n;
    return s;
  }
  template <typename T, typename Fn>
  std::vector<T> vec(Fn readOne) {
    const std::uint32_t n = u32();
    std::vector<T> v;
    v.reserve(n);
    for (std::uint32_t i = 0; i < n && ok(); ++i) v.push_back(readOne(*this));
    return v;
  }

 private:
  std::uint32_t peek_u32(std::size_t off) const {
    std::uint32_t v{};
    std::memcpy(&v, p_ + off, 4);
    return v;
  }
  void raw(void* out, std::size_t n) {
    if (p_ + n > end_) { failed_ = true; std::memset(out, 0, n); return; }
    std::memcpy(out, p_, n);
    p_ += n;
  }
  const std::uint8_t* p_;
  const std::uint8_t* end_;
  std::uint32_t version_{};
  bool magic_ok_{false}, failed_{false};
};

// Contrat d'implémentation par système à état (économie, carrière, arbre...).
// Chaque système écrit/relit SES champs ; le GameState orchestre l'ordre —
// et l'ordre fait partie du schéma (SCHEMA_VERSION à incrémenter s'il change).
class ISerializable {
 public:
  virtual ~ISerializable() = default;
  virtual void save(Writer& w) const = 0;
  virtual void load(Reader& r) = 0;
};

// --- StateHasher -------------------------------------------------------------
// FNV-1a 64 bits sur les octets canoniques de l'archive : le GARDE-FOU du
// déterminisme en CI [carte P3] — même graine, mêmes entrées, même hash,
// toutes plateformes, toutes accélérations temporelles.
inline std::uint64_t fnv1a(const std::uint8_t* data, std::size_t n,
                           std::uint64_t h = 0xcbf29ce484222325ull) {
  for (std::size_t i = 0; i < n; ++i) {
    h ^= data[i];
    h *= 0x100000001b3ull;
  }
  return h;
}
inline std::uint64_t state_hash(const Writer& w) {
  return fnv1a(w.bytes().data(), w.bytes().size());
}

} // namespace fen::save
