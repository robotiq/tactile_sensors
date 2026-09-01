// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <Robotiq/gripper/to_string.hpp>

namespace Robotiq {
namespace {

// Fields render as space-separated name=value; the separator rides with
// the field so each renderer is one line per field.
void appendField(std::string& out, std::string_view name, std::string_view value)
{
   if(!out.empty())
   {
      out += ' ';
   }
   out += name;
   out += '=';
   out += value;
}

void appendBit(std::string& out, std::string_view name, bool on)
{
   appendField(out, name, on ? "1" : "0");
}

void appendByte(std::string& out, std::string_view name, uint8_t value)
{
   appendField(out, name, std::to_string(value));
}

// All four decoded enums fit a nibble, so one hex digit shows the code.
template <class Enum>
void appendEnum(std::string& out, std::string_view name, Enum value)
{
   std::string text(toString(value));
   text += "(0x";
   text += "0123456789ABCDEF"[static_cast<uint8_t>(value) & 0x0F];
   text += ')';
   appendField(out, name, text);
}

} // namespace

std::string toString(const GripperCommand& command)
{
   std::string out;
   appendBit(out, "rACT", command.action.get(ActionRequestBit::Activate));
   appendBit(out, "rGTO", command.action.get(ActionRequestBit::GoTo));
   appendBit(out, "rATR", command.action.get(ActionRequestBit::AutoRelease));
   appendBit(out, "rARD", command.action.get(ActionRequestBit::AutoReleaseOpenDirection));
   appendByte(out, "rPR", command.positionRequest);
   appendByte(out, "rSP", command.speed);
   appendByte(out, "rFR", command.force);
   return out;
}

std::string toString(GripperStatusFlags flags)
{
   std::string out;
   appendBit(out, "gACT", flags.activated());
   appendBit(out, "gGTO", flags.goToEnabled());
   appendEnum(out, "gSTA", flags.activationState());
   appendEnum(out, "gOBJ", flags.objectDetection());
   return out;
}

std::string toString(FaultStatus fault)
{
   std::string out;
   appendEnum(out, "gFLT", fault.gripperFault());
   appendEnum(out, "kFLT", fault.controllerFault());
   return out;
}

std::string toString(const GripperStatus& status)
{
   std::string out = toString(status.gripperStatus);
   out += ' ';
   out += toString(status.faultStatus);
   appendByte(out, "gPR", status.positionRequestEcho);
   appendByte(out, "gPO", status.position);
   appendByte(out, "gCU", status.current);
   return out;
}

} // namespace Robotiq
