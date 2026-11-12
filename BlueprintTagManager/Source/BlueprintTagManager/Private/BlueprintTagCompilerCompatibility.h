#pragma once

// Older UE headers invoke Clang's feature test with newer MSVC headers.
// Use the documented non-Clang fallback before including engine headers.
// https://clang.llvm.org/docs/LanguageExtensions.html#has-feature-and-has-extension
#if defined(_MSC_VER) && !defined(__clang__) && !defined(__has_feature)
#define __has_feature(Feature) 0
#endif
