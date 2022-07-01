#pragma once

#include <c10/macros/Macros.h>
#include "AccumulateType.h"

namespace at {
namespace AtenIpexTypeXPU {

template <typename scalar_t>
C10_HOST_DEVICE static inline scalar_t zeta(scalar_t _x, scalar_t _q) {
  using acc_t = acc_type<scalar_t>;
  const acc_t MACHEP = acc_t{1.11022302462515654042E-16};
  constexpr acc_t zero = acc_t{0.0};
  constexpr acc_t half = acc_t{0.5};
  constexpr acc_t one = acc_t{1.0};
  static const acc_t A[] = {
      12.0,
      -720.0,
      30240.0,
      -1209600.0,
      47900160.0,
      -1.8924375803183791606e9, /*1.307674368e12/691*/
      7.47242496e10,
      -2.950130727918164224e12, /*1.067062284288e16/3617*/
      1.1646782814350067249e14, /*5.109094217170944e18/43867*/
      -4.5979787224074726105e15, /*8.028576626982912e20/174611*/
      1.8152105401943546773e17, /*1.5511210043330985984e23/854513*/
      -7.1661652561756670113e18 /*1.6938241367317436694528e27/236364091*/
  };
  acc_t x = static_cast<acc_t>(_x);
  acc_t q = static_cast<acc_t>(_q);

  int i = 0;
  acc_t a, b, k, s, t, w;
  if (x == one) {
    return std::numeric_limits<scalar_t>::infinity();
  }

  if (x < one) {
    return std::numeric_limits<scalar_t>::quiet_NaN();
  }

  if (q <= zero) {
    if (q == DPCPP::floor(q)) {
      return std::numeric_limits<scalar_t>::infinity();
    }
    if (x != DPCPP::floor(x)) {
      return std::numeric_limits<scalar_t>::quiet_NaN();
    }
  }

  s = DPCPP::pow(q, -x);
  a = q;
  i = 0;
  b = zero;
  while ((i < 9) || (a <= acc_t{9.0})) {
    i += 1;
    a += one;
    b = DPCPP::pow(a, -x);
    s += b;
    if ((-MACHEP * s < b) && (b < MACHEP * s)) {
      return static_cast<scalar_t>(s);
    }
  };

  w = a;
  s += b * w / (x - one);
  s -= half * b;
  a = one;
  k = zero;
  for (int i = 0; i < 12; i++) {
    a *= x + k;
    b /= w;
    t = a * b / A[i];
    s = s + t;
    t = DPCPP::fabs(t / s);
    if (t < MACHEP) {
      return static_cast<scalar_t>(s);
    }
    k += one;
    a *= x + k;
    b /= w;
    k += one;
  }
  return static_cast<scalar_t>(s);
}

template <typename scalar_t>
static inline C10_HOST_DEVICE scalar_t calc_digamma(scalar_t in) {
  // [C++ Standard Reference: Gamma Function]
  // https://en.cppreference.com/w/cpp/numeric/math/tgamma
  using accscalar_t = acc_type<scalar_t>;
  static const double PI_f64 = 3.14159265358979323846;
  const accscalar_t PSI_10 = 2.25175258906672110764;
  const accscalar_t A[] = {
      8.33333333333333333333E-2,
      -2.10927960927960927961E-2,
      7.57575757575757575758E-3,
      -4.16666666666666666667E-3,
      3.96825396825396825397E-3,
      -8.33333333333333333333E-3,
      8.33333333333333333333E-2,
  };

  accscalar_t x = static_cast<accscalar_t>(in);
  if (x == 0) {
    // As per C++ standard for gamma related functions and SciPy,
    // If the argument is ±0, ±∞ is returned
    return std::copysign(static_cast<scalar_t>(INFINITY), -x);
  }

  bool x_is_integer = x == ::trunc(x);
  accscalar_t result = 0;
  if (x < 0) {
    if (x_is_integer) {
      // As per C++ standard for gamma related functions and SciPy,
      // If the argument is a negative integer, NaN is returned
      return static_cast<scalar_t>(NAN);
    }
    // Extracts the fractional part of x as r, since tan(pi * r) is more
    // numerically accurate than tan(pi * x). While these operations are
    // mathematically equivalent since both x and r are in radians and tan() has
    // a periodicity of pi, in practice the computation of pi * x is a source of
    // error (when |x| > 1).
    double q, r;
    r = DPCPP::modf(static_cast<double>(x), &q);
    result = static_cast<accscalar_t>(-PI_f64 / DPCPP::tan(PI_f64 * r));
    x = 1 - x;
  }

  while (x < 10) {
    result -= 1 / x;
    x += 1;
  }
  if (x == 10) {
    return static_cast<scalar_t>(result + PSI_10);
  }

  accscalar_t y = 0;
  if (x < 1.0e17f) {
    accscalar_t z = 1 / (x * x);

    accscalar_t polevl_result = 0;
    for (int i = 0; i <= 6; i++) {
      polevl_result = polevl_result * z + A[i];
    }
    y = z * polevl_result;
  }

  return static_cast<scalar_t>(
      DPCPP::log(x) - (static_cast<accscalar_t>(0.5) / x) - y + result);
}

template <typename scalar_t>
static inline C10_HOST_DEVICE scalar_t calc_trigamma(scalar_t in) {
  using accscalar_t = acc_type<scalar_t>;
  const accscalar_t PI = 3.14159265358979323846;
  accscalar_t x = static_cast<accscalar_t>(in);
  accscalar_t sign = +1;
  accscalar_t result = 0;
  if (x < 0.5f) {
    sign = -1;
    accscalar_t sin_pi_x = DPCPP::sin(PI * x);
    result -= (PI * PI) / (sin_pi_x * sin_pi_x);
    x = 1 - x;
  }
  for (int i = 0; i < 6; ++i) {
    result += 1 / (x * x);
    x += 1;
  }
  const accscalar_t one = static_cast<scalar_t>(1);
  const accscalar_t ixx = 1 / (x * x);
  result += (1 + 1 / (2 * x) +
             ixx * (one / 6 - ixx * (one / 30 - ixx * (one / 42)))) /
      x;
  return static_cast<scalar_t>(sign * result);
}

template <typename scalar_t>
static inline C10_HOST_DEVICE scalar_t calc_gcd(scalar_t a_in, scalar_t b_in) {
  scalar_t a = DPCPP::abs(a_in);
  scalar_t b = DPCPP::abs(b_in);
  while (a != 0) {
    scalar_t c = a;
    a = b % a;
    b = c;
  }
  return b;
}

/*
 * For licensing information and documentation, please refer to the the cpu
 * implementation located in "ATen/native/Math.h".
 */
template <typename scalar_t>
static inline C10_HOST_DEVICE scalar_t
chbevl(scalar_t _x, const scalar_t array[], size_t len) {
  static_assert(
      !std::is_same<scalar_t, Half>() && !std::is_same<scalar_t, BFloat16>(),
      "don't instantiate with low precision type");
  using accscalar_t = acc_type<scalar_t>;

  accscalar_t x = static_cast<accscalar_t>(_x);
  accscalar_t b0, b1, b2;

  b0 = static_cast<accscalar_t>(array[0]);
  b1 = 0;

  for (size_t i = 1; i < len; ++i) {
    b2 = b1;
    b1 = b0;
    b0 = x * b1 - b2 + static_cast<accscalar_t>(array[i]);
  }

  return static_cast<scalar_t>(0.5 * (b0 - b2));
}

/*
 * For licensing information and documentation, please refer to the the cpu
 * implementation located in "ATen/native/Math.h".
 */
template <typename T>
C10_HOST_DEVICE inline std::tuple<const T*, size_t>
chebyshev_coefficients_i0e_A() {
  /* Chebyshev coefficients for exp(-x) I0(x)
   * in the interval [0,8].
   *
   * lim(x->0){ exp(-x) I0(x) } = 1.
   */
  static const T coefficients[] = {
      -4.41534164647933937950E-18, 3.33079451882223809783E-17,
      -2.43127984654795469359E-16, 1.71539128555513303061E-15,
      -1.16853328779934516808E-14, 7.67618549860493561688E-14,
      -4.85644678311192946090E-13, 2.95505266312963983461E-12,
      -1.72682629144155570723E-11, 9.67580903537323691224E-11,
      -5.18979560163526290666E-10, 2.65982372468238665035E-9,
      -1.30002500998624804212E-8,  6.04699502254191894932E-8,
      -2.67079385394061173391E-7,  1.11738753912010371815E-6,
      -4.41673835845875056359E-6,  1.64484480707288970893E-5,
      -5.75419501008210370398E-5,  1.88502885095841655729E-4,
      -5.76375574538582365885E-4,  1.63947561694133579842E-3,
      -4.32430999505057594430E-3,  1.05464603945949983183E-2,
      -2.37374148058994688156E-2,  4.93052842396707084878E-2,
      -9.49010970480476444210E-2,  1.71620901522208775349E-1,
      -3.04682672343198398683E-1,  6.76795274409476084995E-1};

  return std::make_tuple(coefficients, 30);
}

template <typename T>
C10_HOST_DEVICE inline std::tuple<const T*, size_t>
chebyshev_coefficients_i0e_B() {
  /* Chebyshev coefficients for exp(-x) sqrt(x) I0(x)
   * in the inverted interval [8,infinity].
   *
   * lim(x->inf){ exp(-x) sqrt(x) I0(x) } = 1/sqrt(2pi).
   */
  static const T coefficients[] = {
      -7.23318048787475395456E-18, -4.83050448594418207126E-18,
      4.46562142029675999901E-17,  3.46122286769746109310E-17,
      -2.82762398051658348494E-16, -3.42548561967721913462E-16,
      1.77256013305652638360E-15,  3.81168066935262242075E-15,
      -9.55484669882830764870E-15, -4.15056934728722208663E-14,
      1.54008621752140982691E-14,  3.85277838274214270114E-13,
      7.18012445138366623367E-13,  -1.79417853150680611778E-12,
      -1.32158118404477131188E-11, -3.14991652796324136454E-11,
      1.18891471078464383424E-11,  4.94060238822496958910E-10,
      3.39623202570838634515E-9,   2.26666899049817806459E-8,
      2.04891858946906374183E-7,   2.89137052083475648297E-6,
      6.88975834691682398426E-5,   3.36911647825569408990E-3,
      8.04490411014108831608E-1};

  return std::make_tuple(coefficients, 25);
}

template <typename scalar_t>
static inline C10_HOST_DEVICE scalar_t calc_i0(scalar_t _x) {
  static_assert(
      !std::is_same<scalar_t, Half>() && !std::is_same<scalar_t, BFloat16>(),
      "don't instantiate with low precision type");
  // Upcast input for numerical accuracy purposes
  // Needed for accurate results if input is bfloat16 or float16
  scalar_t x = DPCPP::abs(_x);

  if (x <= scalar_t{8.0}) {
    auto coeff_pair = chebyshev_coefficients_i0e_A<scalar_t>();
    auto A = std::get<0>(coeff_pair);
    auto len = std::get<1>(coeff_pair);
    scalar_t y = (x / scalar_t{2.0}) - scalar_t{2.0};
    return (DPCPP::exp(x) * chbevl(y, A, len));
  }

  auto coeff_pair = chebyshev_coefficients_i0e_B<scalar_t>();
  auto B = std::get<0>(coeff_pair);
  auto len = std::get<1>(coeff_pair);
  return (
      DPCPP::exp(x) * chbevl(scalar_t{32.0} / x - scalar_t{2.0}, B, len) /
      DPCPP::sqrt(x));
}

template <typename T>
C10_HOST_DEVICE inline typename std::enable_if<
    std::is_same<double, T>::value,
    std::tuple<const T*, size_t>>::type
chebyshev_coefficients_i1e_A() {
  /* Chebyshev coefficients for exp(-x) I1(x)
   * in the interval [0,8].
   *
   * lim(x->0){ exp(-x) I1(x) / x } = 1/2.
   */
  static const T coefficients[] = {
      2.77791411276104639959E-18, -2.11142121435816608115E-17,
      1.55363195773620046921E-16, -1.10559694773538630805E-15,
      7.60068429473540693410E-15, -5.04218550472791168711E-14,
      3.22379336594557470981E-13, -1.98397439776494371520E-12,
      1.17361862988909016308E-11, -6.66348972350202774223E-11,
      3.62559028155211703701E-10, -1.88724975172282928790E-9,
      9.38153738649577178388E-9,  -4.44505912879632808065E-8,
      2.00329475355213526229E-7,  -8.56872026469545474066E-7,
      3.47025130813767847674E-6,  -1.32731636560394358279E-5,
      4.78156510755005422638E-5,  -1.61760815825896745588E-4,
      5.12285956168575772895E-4,  -1.51357245063125314899E-3,
      4.15642294431288815669E-3,  -1.05640848946261981558E-2,
      2.47264490306265168283E-2,  -5.29459812080949914269E-2,
      1.02643658689847095384E-1,  -1.76416518357834055153E-1,
      2.52587186443633654823E-1};

  return std::make_tuple(coefficients, 29);
}

template <typename T>
C10_HOST_DEVICE inline typename std::
    enable_if<std::is_same<float, T>::value, std::tuple<const T*, size_t>>::type
    chebyshev_coefficients_i1e_A() {
  /* Chebyshev coefficients for exp(-x) I1(x)
   * in the interval [0,8].
   *
   * lim(x->0){ exp(-x) I1(x) / x } = 1/2.
   */
  static const T coeff[] = {
      9.38153738649577178388E-9f,
      -4.44505912879632808065E-8f,
      2.00329475355213526229E-7f,
      -8.56872026469545474066E-7f,
      3.47025130813767847674E-6f,
      -1.32731636560394358279E-5f,
      4.78156510755005422638E-5f,
      -1.61760815825896745588E-4f,
      5.12285956168575772895E-4f,
      -1.51357245063125314899E-3f,
      4.15642294431288815669E-3f,
      -1.05640848946261981558E-2f,
      2.47264490306265168283E-2f,
      -5.29459812080949914269E-2f,
      1.02643658689847095384E-1f,
      -1.76416518357834055153E-1f,
      2.52587186443633654823E-1f};
  return std::make_tuple(coeff, 17);
};

template <typename T>
C10_HOST_DEVICE inline typename std::enable_if<
    std::is_same<double, T>::value,
    std::tuple<const T*, size_t>>::type
chebyshev_coefficients_i1e_B() {
  /* Chebyshev coefficients for exp(-x) sqrt(x) I1(x)
   * in the inverted interval [8,infinity].
   *
   * lim(x->inf){ exp(-x) sqrt(x) I1(x) } = 1/sqrt(2pi).
   */
  static const T coefficients[] = {
      7.51729631084210481353E-18,  4.41434832307170791151E-18,
      -4.65030536848935832153E-17, -3.20952592199342395980E-17,
      2.96262899764595013876E-16,  3.30820231092092828324E-16,
      -1.88035477551078244854E-15, -3.81440307243700780478E-15,
      1.04202769841288027642E-14,  4.27244001671195135429E-14,
      -2.10154184277266431302E-14, -4.08355111109219731823E-13,
      -7.19855177624590851209E-13, 2.03562854414708950722E-12,
      1.41258074366137813316E-11,  3.25260358301548823856E-11,
      -1.89749581235054123450E-11, -5.58974346219658380687E-10,
      -3.83538038596423702205E-9,  -2.63146884688951950684E-8,
      -2.51223623787020892529E-7,  -3.88256480887769039346E-6,
      -1.10588938762623716291E-4,  -9.76109749136146840777E-3,
      7.78576235018280120474E-1};

  return std::make_tuple(coefficients, 25);
}

template <typename T>
C10_HOST_DEVICE inline typename std::
    enable_if<std::is_same<float, T>::value, std::tuple<const T*, size_t>>::type
    chebyshev_coefficients_i1e_B() {
  /* Chebyshev coefficients for exp(-x) sqrt(x) I1(x)
   * in the inverted interval [8,infinity].
   *
   * lim(x->inf){ exp(-x) sqrt(x) I1(x) } = 1/sqrt(2pi).
   */
  static const T coeff[] = {
      -3.83538038596423702205E-9f,
      -2.63146884688951950684E-8f,
      -2.51223623787020892529E-7f,
      -3.88256480887769039346E-6f,
      -1.10588938762623716291E-4f,
      -9.76109749136146840777E-3f,
      7.78576235018280120474E-1f};

  return std::make_tuple(coeff, 7);
};

template <typename scalar_t>
static inline C10_HOST_DEVICE scalar_t calc_i1(scalar_t _x) {
  const auto x = DPCPP::abs(_x);
  if (x <= scalar_t{8.0}) {
    auto coeff_pair = chebyshev_coefficients_i1e_A<scalar_t>();
    auto A = std::get<0>(coeff_pair);
    auto len = std::get<1>(coeff_pair);
    scalar_t y = x / scalar_t{2.0} - scalar_t{2.0};
    const scalar_t out = DPCPP::exp(x) * x * chbevl(y, A, len);
    return (_x < scalar_t{0.0}) ? -out : out;
  }

  auto coeff_pair = chebyshev_coefficients_i1e_B<scalar_t>();
  auto B = std::get<0>(coeff_pair);
  auto len = std::get<1>(coeff_pair);
  const scalar_t out =
      (DPCPP::exp(x) * chbevl(scalar_t{32.0} / x - scalar_t{2.0}, B, len)) /
      DPCPP::sqrt(x);
  return (_x < scalar_t{0.0}) ? -out : out;
}

template <typename scalar_t>
static inline C10_HOST_DEVICE scalar_t calc_i1e(scalar_t _x) {
  const auto x = DPCPP::abs(_x);
  if (x <= scalar_t{8.0}) {
    auto coeff_pair = chebyshev_coefficients_i1e_A<scalar_t>();
    auto A = std::get<0>(coeff_pair);
    auto len = std::get<1>(coeff_pair);
    const scalar_t y = x / scalar_t{2.0} - scalar_t{2.0};
    const scalar_t out = chbevl(y, A, len) * x;
    return (_x < scalar_t{0.0}) ? -out : out;
  }

  auto coeff_pair = chebyshev_coefficients_i1e_B<scalar_t>();
  auto B = std::get<0>(coeff_pair);
  auto len = std::get<1>(coeff_pair);
  const scalar_t out =
      chbevl(scalar_t{32.0} / x - scalar_t{2.0}, B, len) / DPCPP::sqrt(x);
  return (_x < scalar_t{0.0}) ? -out : out;
}

template <typename scalar_t>
static inline C10_HOST_DEVICE scalar_t calc_polygamma(scalar_t x, int n) {
  // already blocked if n <= 1
  const auto one = scalar_t{1};
  return ((n % 2) ? one : -one) *
      DPCPP::exp(std::lgamma(static_cast<scalar_t>(n) + one)) *
      zeta<scalar_t>(static_cast<scalar_t>(n + 1), x);
}

} // namespace AtenIpexTypeXPU
} // namespace at
