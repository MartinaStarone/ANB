add_subdirectory(Utils Utils)

# EDDI
add_library(EDDI SHARED
			DuplicateGlobals.cpp
			EDDI.cpp
			FuncRetToRef.cpp
			Utils/Utils.cpp
)
target_compile_definitions(EDDI PRIVATE SELECTIVE_CHECKING=0 CHECK_AT_STORES CHECK_AT_CALLS CHECK_AT_BRANCH)

# FDSC
add_library(FDSC SHARED
			DuplicateGlobals.cpp
			EDDI.cpp
			FuncRetToRef.cpp
			Utils/Utils.cpp
)
target_compile_definitions(FDSC PRIVATE SELECTIVE_CHECKING=1 CHECK_AT_STORES CHECK_AT_CALLS CHECK_AT_BRANCH)

# sEDDI
add_library(SEDDI SHARED
			DuplicateGlobals.cpp
			EDDI.cpp
			FuncRetToRef.cpp
			Utils/Utils.cpp
)
target_compile_definitions(SEDDI PRIVATE SELECTIVE_CHECKING=0 CHECK_AT_CALLS CHECK_AT_BRANCH)


# CFCSS
add_library(CFCSS SHARED
			CFCSS.cpp
			Utils/Utils.cpp
)

# RASM
add_library(RASM SHARED
			RASM.cpp
			Utils/Utils.cpp
)
target_compile_definitions(RASM PRIVATE INTER_FUNCTION_CFC=0)


# inter-RASM
add_library(INTER_RASM SHARED
			RASM.cpp
			Utils/Utils.cpp
)
target_compile_definitions(INTER_RASM PRIVATE INTER_FUNCTION_CFC=1)

# RACFED
add_library(RACFED SHARED
			RACFED.cpp
			Utils/Utils.cpp
)

# ANB — Arithmetic Necessary Basis encoding pass
# Must be loaded AFTER RACFED in the opt pipeline:
#   opt -load-pass-plugin=libRACFED.so -load-pass-plugin=libANB.so \
#       -passes="racfed-verify,anb-encode" ...
# NOTE: link against libUtils.so (do NOT re-compile Utils.cpp here)
#       to avoid double-registration of llvm::cl::opt globals when
#       both libRACFED.so and libANB.so are loaded by opt.
add_library(ANB SHARED
		Utils/ANB.cpp
		../AES_testing/aes.c
		../AES_testing/main_test.c
)
target_link_libraries(ANB PRIVATE Utils)

add_library(PROFILER SHARED
			Profiling/ASPISCheckProfiler.cpp
			Utils/Utils.cpp
)
