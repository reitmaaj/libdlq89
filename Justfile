set shell := ["sh", "-eu", "-c"]

CC := env_var_or_default("CC", "cc")
STRICT := "-std=c89 -pedantic-errors -Wall -Wextra -Werror -Wconversion -Wsign-conversion -Wstrict-prototypes -Wmissing-prototypes -Wold-style-definition -Wundef -Wshadow -Wformat=2 -Wno-long-long"
INCS := "-Iinclude -Isrc -I../libunicode89/include"
SRC := "src/dlq89_api.c src/dlq89_diag.c src/dlq89_eval.c src/dlq89_mem.c src/dlq89_parse.c src/dlq89_status.c"

default: build

unicode89:
    cd ../libunicode89 && just build

build: unicode89
    mkdir -p build
    @for f in {{SRC}}; do \
        name=$(basename "$f" .c); \
        {{CC}} {{STRICT}} {{INCS}} -c "$f" -o "build/$name.o" || exit 1; \
    done
    rm -f build/libdlq89.a
    ar rcs build/libdlq89.a build/dlq89_api.o build/dlq89_diag.o build/dlq89_eval.o build/dlq89_mem.o build/dlq89_parse.o build/dlq89_status.o

headers:
    mkdir -p build
    gcc {{STRICT}} -Iinclude -x c -include include/dlq89.h -c /dev/null -o /dev/null
    clang {{STRICT}} -Iinclude -x c -include include/dlq89.h -c /dev/null -o /dev/null

smoke: build
    {{CC}} {{STRICT}} {{INCS}} -o build/smoke test/smoke.c build/libdlq89.a ../libunicode89/build/libunicode89.a
    ./build/smoke

syntax: build
    {{CC}} {{STRICT}} {{INCS}} -o build/syntax test/syntax.c build/libdlq89.a ../libunicode89/build/libunicode89.a
    ./build/syntax

failure: build
    {{CC}} {{STRICT}} {{INCS}} -o build/failure test/failure.c build/libdlq89.a ../libunicode89/build/libunicode89.a
    ./build/failure

typed: build
    {{CC}} {{STRICT}} {{INCS}} -o build/typed test/typed.c build/libdlq89.a ../libunicode89/build/libunicode89.a
    ./build/typed

test: headers smoke syntax failure typed

clean:
    rm -rf build
