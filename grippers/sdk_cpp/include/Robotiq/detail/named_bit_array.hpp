// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Manipulate individual bits of a value through a named-bit
//!        enum whose enumerators are bit indices. Standard-layout and
//!        trivially copyable, so it composes into wire-mapped blocks.
//!        Internal: it is how the command block's packed action byte is
//!        stored, and callers reach it through that byte's set()/get().

#pragma once

#include <cassert>
#include <limits>
#include <type_traits>

namespace Robotiq::detail {

template <class BitEnum>
class NamedBitArray
{
public:
   using Underlying = std::underlying_type_t<BitEnum>;

   constexpr NamedBitArray() = default;
   constexpr explicit NamedBitArray(Underlying value)
      : _value(value)
   {
   }

   [[nodiscard]] constexpr bool get(BitEnum bit) const { return (_value & mask(bit)) != 0; }
   constexpr void set(BitEnum bit) { _value = static_cast<Underlying>(_value | mask(bit)); }
   constexpr void unset(BitEnum bit) { _value = static_cast<Underlying>(_value & ~mask(bit)); }
   constexpr void set(BitEnum bit, bool on) { on ? set(bit) : unset(bit); }

   [[nodiscard]] constexpr Underlying value() const { return _value; }
   [[nodiscard]] constexpr bool operator==(NamedBitArray other) const { return _value == other._value; }
   [[nodiscard]] constexpr bool operator!=(NamedBitArray other) const { return _value != other._value; }

private:
   static constexpr Underlying mask(BitEnum bit)
   {
      assert(static_cast<unsigned>(bit) < std::numeric_limits<Underlying>::digits);
      return static_cast<Underlying>(1U << static_cast<unsigned>(bit));
   }

   Underlying _value = 0;
};

} // namespace Robotiq::detail
