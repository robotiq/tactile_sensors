// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Manipulate individual bits of a value through a named-bit
//!        enum whose enumerators are bit indices. Standard-layout and
//!        trivially copyable, so it composes into wire-mapped blocks.

#pragma once

#include <type_traits>

namespace Robotiq {

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
   static constexpr Underlying mask(BitEnum bit) { return static_cast<Underlying>(1U << static_cast<Underlying>(bit)); }

   Underlying _value = 0;
};

} // namespace Robotiq
