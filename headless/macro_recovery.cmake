# Keep exceptions on C++ frames: generated macro code has no unwind tables.
file(READ "${MACRO_INPUT}" macro_source)
macro(macro_replace old new)
    string(FIND "${macro_source}" "${old}" offset)
    if(offset LESS 0)
        message(FATAL_ERROR "Pinned macro recovery pattern changed: ${old}")
    endif()
    string(REPLACE "${old}" "${new}" macro_source "${macro_source}")
endmacro()
string(PREPEND macro_source "#include <exception>\n")
# PS5 firmware 7.40 refuses mprotect(RX) on an app that is not jailbroken, so the first
# macro the x64 JIT compiled ended the GPU worker with xbyak's "can't protect". The
# JIT is still tried first, since it is the faster way where it works (macro-heavy games
# lost speed when every console got the interpreter); the first refusal turns it off for the
# rest of the session and that macro, like the ones after it, runs through the interpreter,
# which needs no executable memory.
string(PREPEND macro_source "#include <cstdio>\n")
macro_replace("    if (!is_interpreted)\n        return std::make_unique<MacroJITx64Impl>(system, code);\n"
    "    if (!is_interpreted) {\n        try {\n            return std::make_unique<MacroJITx64Impl>(system, code);\n        } catch (const std::exception& refused) {\n            std::fprintf(stderr, \"[ProsperoEden] graphics: the macro compiler is not usable on this console (%s); macros are interpreted%c\", refused.what(), 10);\n            is_interpreted = true;\n        }\n    }\n")
macro_replace("        u32 carry_flag{};"
    "        u32 carry_flag{};\n        std::exception_ptr failure;\n        bool failed{};")
macro_replace("    program(&state, parameters.data(), parameters.data() + parameters.size());"
    "    program(&state, parameters.data(), parameters.data() + parameters.size());\n    if (state.failure) std::rethrow_exception(state.failure);")
macro_replace("static void MacroJIT_SendThunk(Core::System* system, Engines::Maxwell3D* maxwell3d, Macro::MethodAddress method_address, u32 value) {\n    maxwell3d->CallMethod(*system, method_address.address, value, true);\n}"
    "static void MacroJIT_SendThunk(MacroJITx64Impl::JITState* state, Macro::MethodAddress method_address, u32 value) noexcept {\n    try {\n        state->maxwell3d->CallMethod(*state->system, method_address.address, value, true);\n    } catch (...) {\n        state->failure = std::current_exception();\n        state->failed = true;\n    }\n}")
macro_replace("    mov(Common::X64::ABI_PARAM1, qword[STATE + offsetof(JITState, system)]);\n    mov(Common::X64::ABI_PARAM2, qword[STATE + offsetof(JITState, maxwell3d)]);\n    mov(Common::X64::ABI_PARAM3.cvt32(), METHOD_ADDRESS);\n    mov(Common::X64::ABI_PARAM4.cvt32(), value);"
    "    mov(Common::X64::ABI_PARAM3.cvt32(), value);\n    mov(Common::X64::ABI_PARAM2.cvt32(), METHOD_ADDRESS);\n    mov(Common::X64::ABI_PARAM1, STATE);")
macro_replace("    Common::X64::ABI_PopRegistersAndAdjustStack(*this, PersistentCallerSavedRegs(), 0);\n\n    Xbyak::Label dont_process{};"
    "    Common::X64::ABI_PopRegistersAndAdjustStack(*this, PersistentCallerSavedRegs(), 0);\n    cmp(byte[STATE + offsetof(JITState, failed)], 0);\n    jne(end_of_code, T_NEAR);\n\n    Xbyak::Label dont_process{};")
file(WRITE "${MACRO_OUTPUT}.in" "${macro_source}")
configure_file("${MACRO_OUTPUT}.in" "${MACRO_OUTPUT}" COPYONLY)
