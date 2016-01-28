/// GCC does not understand the __OSX_AVAILABLE_STARTING declared in
/// Availability.h. This includes the file and then replaces the definition.
/// Caveat: No warning/error if you try to use a function or constant not
/// present your deployment target.
#if defined(INFINIT_MACOSX) && defined(__GNUC__) && !defined(__clang__)
# include <Availability.h>
# ifdef __OSX_AVAILABLE_STARTING
#  undef __OSX_AVAILABLE_STARTING
#  define __OSX_AVAILABLE_STARTING(_osx, _ios)
# endif
#endif

/// Building on OS X 10.10 requires __has_extension which GCC does not have.
#ifndef __has_extension
# define __has_extension(x) 0
#endif

/// Building on OS X 10.10 requires __has_feature which GCC does not have.
#ifndef __has_feature
# define __has_feature(x) 0
#endif
