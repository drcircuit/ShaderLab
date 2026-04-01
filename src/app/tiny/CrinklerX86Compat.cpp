#if defined(_M_IX86)
#include <cstddef>
#include <cstdlib>
#include <cmath>
#include <new>

extern "C" int __cdecl _filter_x86_sse2_floating_point_exception() {
    return 0;
}

extern "C" int __cdecl __filter_x86_sse2_floating_point_exception() {
    return _filter_x86_sse2_floating_point_exception();
}

extern "C" void __cdecl _invoke_watson(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {
    std::abort();
}

void __stdcall _CxxThrowException(void*, void*) {
    std::abort();
}

extern "C" void __cdecl __std_exception_destroy(void*) {
}

extern "C" void __cdecl __std_exception_copy(void*, const void*) {
}

extern "C" int __cdecl _register_onexit_function(void**, void (__cdecl*)(void)) {
    return 0;
}

extern "C" int __cdecl _crt_atexit(void (__cdecl*)(void), void*, void*) {
    return 0;
}

extern "C" long __cdecl _lround_compat(double x) {
    return std::lround(x);
}

extern "C" void __cdecl _invalid_parameter_noinfo_noreturn() {
    std::abort();
}

extern "C" void (__cdecl* _imp___invoke_watson)(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) = _invoke_watson;
extern "C" long (__cdecl* _imp__lround)(double) = _lround_compat;
extern "C" void (__cdecl* _imp___invalid_parameter_noinfo_noreturn)() = _invalid_parameter_noinfo_noreturn;

void* __cdecl operator new(std::size_t size) {
    if (size == 0) {
        size = 1;
    }
    if (void* p = std::malloc(size)) {
        return p;
    }
    std::abort();
}

void* __cdecl operator new[](std::size_t size) {
    return operator new(size);
}

void* __cdecl operator new(std::size_t size, const std::nothrow_t&) noexcept {
    if (size == 0) {
        size = 1;
    }
    return std::malloc(size);
}

void* __cdecl operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return operator new(size, std::nothrow);
}

void __cdecl operator delete(void* ptr) noexcept {
    std::free(ptr);
}

void __cdecl operator delete[](void* ptr) noexcept {
    std::free(ptr);
}

void __cdecl operator delete(void* ptr, std::size_t) noexcept {
    std::free(ptr);
}

void __cdecl operator delete[](void* ptr, std::size_t) noexcept {
    std::free(ptr);
}

void __cdecl operator delete(void* ptr, const std::nothrow_t&) noexcept {
    std::free(ptr);
}

void __cdecl operator delete[](void* ptr, const std::nothrow_t&) noexcept {
    std::free(ptr);
}
#endif
