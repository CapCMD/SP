// fen/core/Matrix.hpp
// Algèbre 6x6 minimale pour la détermination d'orbite. Rien de plus.
// (Pas d'Eigen : une dépendance de 100 000 lignes pour inverser du 6x6 serait
//  une faute de goût, et casserait la promesse "zéro dépendance".)
#pragma once
#include <cmath>
#include <cstddef>
#include "fen/core/Vec3.hpp"

namespace fen {

struct Vec6 {
  double v[6]{};
  double& operator[](int i) { return v[i]; }
  double operator[](int i) const { return v[i]; }
  static Vec6 from(const Vec3& r, const Vec3& vv) {
    Vec6 x; x[0]=r.x; x[1]=r.y; x[2]=r.z; x[3]=vv.x; x[4]=vv.y; x[5]=vv.z; return x;
  }
  Vec3 pos() const { return {v[0], v[1], v[2]}; }
  Vec3 vel() const { return {v[3], v[4], v[5]}; }
  Vec6 operator+(const Vec6& o) const { Vec6 r; for (int i=0;i<6;++i) r[i]=v[i]+o[i]; return r; }
  Vec6 operator-(const Vec6& o) const { Vec6 r; for (int i=0;i<6;++i) r[i]=v[i]-o[i]; return r; }
};

struct Mat6 {
  double m[6][6]{};

  static Mat6 identity() { Mat6 a; for (int i=0;i<6;++i) a.m[i][i]=1.0; return a; }
  static Mat6 zero() { return Mat6{}; }

  Mat6 operator*(const Mat6& b) const {
    Mat6 c;
    for (int i=0;i<6;++i) for (int j=0;j<6;++j) {
      double s=0.0; for (int k=0;k<6;++k) s += m[i][k]*b.m[k][j];
      c.m[i][j]=s;
    }
    return c;
  }
  Vec6 operator*(const Vec6& x) const {
    Vec6 y;
    for (int i=0;i<6;++i) { double s=0.0; for (int k=0;k<6;++k) s += m[i][k]*x[k]; y[i]=s; }
    return y;
  }
  Mat6 transpose() const {
    Mat6 t; for (int i=0;i<6;++i) for (int j=0;j<6;++j) t.m[i][j]=m[j][i]; return t;
  }
  Mat6 operator+(const Mat6& b) const {
    Mat6 c; for (int i=0;i<6;++i) for (int j=0;j<6;++j) c.m[i][j]=m[i][j]+b.m[i][j]; return c;
  }
};

// Inversion Gauss-Jordan avec pivot partiel. Renvoie false si singulière —
// et une matrice normale singulière SIGNIFIE QUELQUE CHOSE : l'orbite n'est pas
// observable avec les mesures achetées. Ce n'est pas une erreur, c'est un
// résultat, et il doit remonter au joueur.
inline bool invert6(const Mat6& a, Mat6& inv) {
  double A[6][12];
  for (int i=0;i<6;++i) {
    for (int j=0;j<6;++j) A[i][j] = a.m[i][j];
    for (int j=0;j<6;++j) A[i][6+j] = (i==j) ? 1.0 : 0.0;
  }
  for (int c=0;c<6;++c) {
    int p = c;
    for (int r=c+1;r<6;++r) if (std::fabs(A[r][c]) > std::fabs(A[p][c])) p = r;
    if (std::fabs(A[p][c]) < 1e-300) return false;
    if (p != c) for (int j=0;j<12;++j) std::swap(A[c][j], A[p][j]);
    const double d = A[c][c];
    for (int j=0;j<12;++j) A[c][j] /= d;
    for (int r=0;r<6;++r) {
      if (r == c) continue;
      const double f = A[r][c];
      if (f == 0.0) continue;
      for (int j=0;j<12;++j) A[r][j] -= f * A[c][j];
    }
  }
  for (int i=0;i<6;++i) for (int j=0;j<6;++j) inv.m[i][j] = A[i][6+j];
  return true;
}

// Écarts-types marginaux (racines de la diagonale de la covariance).
inline double sigma_position(const Mat6& P) {
  return std::sqrt(std::fmax(0.0, P.m[0][0] + P.m[1][1] + P.m[2][2]));
}
inline double sigma_velocity(const Mat6& P) {
  return std::sqrt(std::fmax(0.0, P.m[3][3] + P.m[4][4] + P.m[5][5]));
}

} // namespace fen
