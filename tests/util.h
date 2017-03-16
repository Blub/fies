#ifndef FIES_TESTS_UTIL_H
#define FIES_TESTS_UTIL_H

#include <stdint.h>
#include <inttypes.h>

#include <type_traits>
#include <utility>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

using std::move;
using std::nullptr_t;
using std::string;
using std::vector;

template<typename T, typename Del = std::default_delete<T>>
using uniq = std::unique_ptr<T, Del>;

template<typename A, typename B>
using map = std::unordered_map<A, B>;

template<typename T, typename U>
static inline constexpr T reinter(U&& u) {
	return reinterpret_cast<T>(std::forward<U>(u));
}

template<typename T, typename U>
static inline constexpr T cast(U&& u) {
	return static_cast<T>(std::forward<U>(u));
}

namespace num {

template<typename T>
struct NumberIterator {
	T value_;

	NumberIterator() = delete;
	constexpr NumberIterator(T value) : value_(value) {}

	T operator*() const { return value_; }
	operator T() const { return value_; }

	NumberIterator& operator++() { ++value_; return (*this); }
	NumberIterator& operator--() { --value_; return (*this); }
	T operator++(int) { return value_++; }
	T operator--(int) { return value_--; }

	NumberIterator& operator-=(T v) { value_ -= v; return (*this); }
	NumberIterator& operator+=(T v) { value_ += v; return (*this); }
	NumberIterator& operator*=(T v) { value_ *= v; return (*this); }
	NumberIterator& operator/=(T v) { value_ /= v; return (*this); }

	T operator-(T v) const { return value_ - v; }
	T operator+(T v) const { return value_ + v; }
	T operator*(T v) const { return value_ * v; }
	T operator/(T v) const { return value_ / v; }

	bool operator< (T v) const { return value_ < v; }
	bool operator<=(T v) const { return value_ <= v; }
	bool operator==(T v) const { return value_ == v; }
	bool operator!=(T v) const { return value_ != v; }
	bool operator>=(T v) const { return value_ >= v; }
	bool operator> (T v) const { return value_ >  v; }
};

template<typename T>
struct Range {
	using Value = T;
	using Iter = NumberIterator<Value>;

	Iter iter_;
	Iter end_;

	Range() = delete;

	constexpr Range(Value end) : iter_(0), end_(end) {}
	constexpr Range(Value beg, Value end) : iter_(beg), end_(end) {}
	constexpr auto begin() const { return iter_; }
	constexpr auto end() const { return end_; }
};

template<typename T> static inline constexpr Range<T>
range(T beg) { return {beg}; }

template<typename T> static inline constexpr Range<T>
range(T beg, T end) { return {beg, end}; }

} // ::num

#endif
