namespace __cxxabiv1 {
__extension__ using __guard = int __attribute__((mode(__DI__)));

extern "C" int __cxa_guard_acquire(__guard* g) {
    return !*reinterpret_cast<char*>(g);
}

extern "C" void __cxa_guard_release(__guard* g) {
    *reinterpret_cast<char*>(g) = 1;
}

extern "C" void __cxa_guard_abort(__guard*) {}
}  // namespace __cxxabiv1

extern "C" void __cxa_pure_virtual() {
    // Do nothing
}