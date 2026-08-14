"""
    soft_fneg(a::UInt64) -> UInt64

IEEE 754 negation: flip the sign bit (bit 63).
"""
soft_fneg(a::UInt64)::UInt64 = a ⊻ UInt64(0x8000000000000000)
