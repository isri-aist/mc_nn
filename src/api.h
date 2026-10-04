#pragma once

#if defined _WIN32 || defined __CYGWIN__
#  define MCNN_DLLIMPORT __declspec(dllimport)
#  define MCNN_DLLEXPORT __declspec(dllexport)
#  define MCNN_DLLLOCAL
#else
// On Linux, for GCC >= 4, tag symbols using GCC extension.
#  if __GNUC__ >= 4
#    define MCNN_DLLIMPORT __attribute__((visibility("default")))
#    define MCNN_DLLEXPORT __attribute__((visibility("default")))
#    define MCNN_DLLLOCAL __attribute__((visibility("hidden")))
#  else
// Otherwise (GCC < 4 or another compiler is used), export everything.
#    define MCNN_DLLIMPORT
#    define MCNN_DLLEXPORT
#    define MCNN_DLLLOCAL
#  endif // __GNUC__ >= 4
#endif // defined _WIN32 || defined __CYGWIN__

#ifdef MCNN_STATIC
// If one is using the library statically, get rid of
// extra information.
#  define MCNN_DLLAPI
#  define MCNN_LOCAL
#else
// Depending on whether one is building or using the
// library define DLLAPI to import or export.
#  ifdef MCNN_EXPORTS
#    define MCNN_DLLAPI MCNN_DLLEXPORT
#  else
#    define MCNN_DLLAPI MCNN_DLLIMPORT
#  endif // MCNN_EXPORTS
#  define MCNN_LOCAL MCNN_DLLLOCAL
#endif // MCNN_STATIC