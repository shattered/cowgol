local posix = require("posix")

local out = io.stdout

local function emit(...)
    for _, s in ipairs({...}) do
        if type(s) == "table" then
            emit(unpack(s))
        else
            out:write(s, " ")
        end
    end
end

local function nl()
    out:write("\n")
end

out:write([[
#############################################################################
###                   THIS FILE IS AUTOGENERATED                          ###
#############################################################################
#
# Don't edit it. Your changes will be destroyed. Instead, edit mkninja.sh
# instead. Next time you run ninja, this file will be automatically updated.

rule mkninja
    command = lua ./mkninja.lua > $out
    generator = true
build build.ninja : mkninja mkninja.lua

OBJDIR = /tmp/cowgol-obj

rule bootstrapped_cowgol_program
    command = scripts/cowgol_bootstrap_compiler -o $out $in

build dependencies_for_bootstrapped_cowgol_program : phony $
    scripts/cowgol_bootstrap_compiler $
    bootstrap/bootstrap.lua $
    bootstrap/cowgol.c $
    bootstrap/cowgol.h

rule cowgol_program
    command = scripts/cowgol -a $arch -o $out $in

build dependencies_for_cowgol_program : phony $
    scripts/cowgol

build compiler_for_native_to_native : phony dependencies_for_bootstrapped_cowgol_program

rule c_program
    command = cc -std=c99 -Wno-unused-result -g -o $out $in

rule token_maker
    command = gawk -f src/mk-token-maker.awk $in > $out

rule token_names
    command = gawk -f src/mk-token-names.awk $in > $out

build $OBJDIR/token_maker.cow : token_maker src/tokens.txt | src/mk-token-maker.awk
build $OBJDIR/token_names.cow : token_names src/tokens.txt | src/mk-token-names.awk
    
rule run_smart_test
    command = $in && touch $out

]])

local NAME
local HOST
local TARGET

local LIBS
local RULE

local GLOBALS
local CODEGEN
local CLASSIFIER
local SIMPLIFIER
local PLACER
local EMITTER

local host_data = {
    ["native"] = function()
        LIBS = {
            "src/arch/bootstrap/host.cow",
            "src/utils/names.cow"
        }

        RULE = "bootstrapped_cowgol_program"
    end,

    ["bbc"] = function()
        LIBS = {
            "src/arch/bbc/host.cow",
            "src/arch/bbc/lib/mos.cow",
            "src/arch/bbc/lib/runtime.cow",
            "src/arch/bbc/lib/fileio.cow",
            "src/arch/bbc/lib/argv.cow",
            "src/arch/bbc/names.cow"
        }

        RULE = "cowgol_program"
    end,
}

local target_data = {
    ["bbc"] = function()
        GLOBALS = "src/arch/bbc/globals.cow"
        CLASSIFIER = "src/arch/bbc/classifier.cow"
        SIMPLIFIER = "src/arch/bbc/simplifier.cow"
        PLACER = "src/arch/bbc/placer.cow"
        EMITTER = "src/arch/6502/emitter.cow"

        CODEGEN = {
            "src/arch/bbc/codegen0.cow",
            "src/arch/bbc/codegen1.cow",
            "src/arch/bbc/codegen2_8bit.cow",
            "src/arch/bbc/codegen2_wide.cow",
            "src/arch/bbc/codegen2_16bit.cow",
            "src/arch/bbc/codegen2.cow",
        }
    end
}

local function build_cowgol(files)
    local program = table.remove(files, 1)
    emit("build", "bin/"..NAME.."/"..program, ":", RULE, LIBS, files,
        "|", "compiler_for_native_to_"..HOST)
    nl()
    emit(" arch =", "native_to_"..HOST)
    nl()
    nl()
end

local function build_c(files)
    local program = table.remove(files, 1)
    emit("build", "bin/"..program, ":", "c_program", files)
    nl()
    nl()
end

local function bootstrap_test(file)
    local testname = file:gsub("^.*/([^./]*)%..*$", "%1")
    local testbin = "$OBJDIR/tests/bootstrap/"..testname
    emit("build", testbin, ":", "bootstrapped_cowgol_program",
        "tests/bootstrap/_test.cow",
        file,
        "|", "dependencies_for_bootstrapped_cowgol_program")
    nl()
    emit("build", testbin..".stamp", ":", "run_smart_test", testbin)
    nl()
    nl()
end

local function cpu_test(file)
end

local function build_cowgol_programs()
    build_cowgol {
        "init",
        "src/string_lib.cow",
        GLOBALS,
        "src/utils/stringtablewriter.cow",
        "src/utils/things.cow",
        "src/utils/iops.cow",
        "src/init/init.cow",
        "$OBJDIR/token_names.cow",
        "src/init/things.cow",
        "$OBJDIR/token_maker.cow",
        "src/init/main.cow",
    }

    build_cowgol {
        "tokeniser",
        "src/string_lib.cow",
        "src/ctype_lib.cow",
        "src/numbers_lib.cow",
        GLOBALS,
        "src/utils/stringtablewriter.cow",
        "src/utils/things.cow",
        "src/tokeniser/lexer.cow",
        "$OBJDIR/token_names.cow",
        "src/tokeniser/tokeniser.cow",
        "src/tokeniser/main.cow",
    }

    build_cowgol {
        "parser",
        "src/string_lib.cow",
        "src/ctype_lib.cow",
        "src/numbers_lib.cow",
        GLOBALS,
        "src/utils/stringtable.cow",
        "src/utils/things.cow",
        "$OBJDIR/token_names.cow",
        "src/utils/iops.cow",
        "src/parser/init.cow",
        "src/parser/symbols.cow",
        "src/utils/symbols.cow",
        "src/parser/iopwriter.cow",
        "src/parser/tokenreader.cow",
        "src/parser/constant.cow",
        "src/utils/types.cow",
        "src/parser/types.cow",
        "src/parser/expression.cow",
        "src/parser/main.cow",
        "src/parser/deinit.cow",
    }

    build_cowgol {
        "blockifier",
        "src/string_lib.cow",
        GLOBALS,
        "src/utils/stringtable.cow",
        "src/utils/things.cow",
        "src/utils/iops.cow",
        "src/utils/iopreader.cow",
        "src/utils/iopwriter.cow",
        "src/utils/symbols.cow",
        "$OBJDIR/token_names.cow",
        "src/utils/types.cow",
        "src/blockifier/init.cow",
        "src/blockifier/main.cow",
        "src/blockifier/deinit.cow",
    }

    build_cowgol {
        "typechecker",
        "src/string_lib.cow",
        GLOBALS,
        "src/utils/stringtable.cow",
        "src/utils/things.cow",
        "src/utils/iops.cow",
        "src/utils/iopreader.cow",
        "src/utils/iopwriter.cow",
        "src/utils/symbols.cow",
        "$OBJDIR/token_names.cow",
        "src/utils/types.cow",
        "src/typechecker/init.cow",
        "src/typechecker/stack.cow",
        "src/typechecker/main.cow",
        "src/typechecker/deinit.cow",
    }

    build_cowgol {
        "backendify",
        "src/string_lib.cow",
        GLOBALS,
        "src/utils/stringtable.cow",
        "src/utils/things.cow",
        "src/utils/iops.cow",
        "src/utils/iopreader.cow",
        "src/utils/iopwriter.cow",
        "src/utils/symbols.cow",
        "$OBJDIR/token_names.cow",
        "src/utils/types.cow",
        "src/backendify/init.cow",
        "src/backendify/temporaries.cow",
        "src/backendify/tree.cow",
        SIMPLIFIER,
        "src/backendify/simplifier.cow",
        "src/backendify/main.cow",
        "src/backendify/deinit.cow",
    }

    build_cowgol {
        "classifier",
        "src/string_lib.cow",
        GLOBALS,
        "src/utils/stringtable.cow",
        "src/utils/things.cow",
        "src/utils/iops.cow",
        "src/utils/iopreader.cow",
        "src/utils/symbols.cow",
        "src/utils/types.cow",
        "$OBJDIR/token_names.cow",
        "src/classifier/init.cow",
        "src/classifier/graph.cow",
        CLASSIFIER,
        "src/classifier/subdata.cow",
        "src/classifier/main.cow",
        "src/classifier/deinit.cow",
    }

    build_cowgol {
        "codegen",
        "src/string_lib.cow",
        GLOBALS,
        "src/utils/stringtable.cow",
        "src/utils/things.cow",
        "src/utils/iops.cow",
        "src/utils/iopreader.cow",
        "src/utils/iopwriter.cow",
        "$OBJDIR/token_names.cow",
        "src/utils/symbols.cow",
        "src/utils/types.cow",
        "src/codegen/init.cow",
        "src/codegen/queue.cow",
        CODEGEN,
        "src/codegen/rules.cow",
        "src/codegen/main.cow",
        "src/codegen/deinit.cow",
    }

    build_cowgol {
        "placer",
        "src/string_lib.cow",
        GLOBALS,
        "src/utils/stringtable.cow",
        "src/utils/things.cow",
        "src/utils/iops.cow",
        "src/utils/iopreader.cow",
        "src/utils/iopwriter.cow",
        "src/placer/init.cow",
        PLACER,
        "src/placer/main.cow",
        "src/placer/deinit.cow",
    }

    build_cowgol {
        "emitter",
        "src/string_lib.cow",
        GLOBALS,
        "src/utils/stringtable.cow",
        "src/utils/things.cow",
        "src/utils/iops.cow",
        "src/utils/iopreader.cow",
        "src/emitter/init.cow",
        EMITTER,
        "src/emitter/main.cow",
        "src/emitter/deinit.cow",
    }

    build_cowgol {
        "thingshower",
        "src/string_lib.cow",
        GLOBALS,
        "src/utils/stringtable.cow",
        "src/utils/things.cow",
        "src/thingshower/thingshower.cow",
    }

    build_cowgol {
        "iopshower",
        "src/string_lib.cow",
        GLOBALS,
        "src/utils/stringtable.cow",
        "src/utils/things.cow",
        "src/utils/iops.cow",
        "src/iopshower/iopreader.cow",
        "src/iopshower/iopshower.cow",
    }
end

-- Build all the combinations of compilers.
for host, hostcb in pairs(host_data) do
    HOST = host
    hostcb()

    for target, targetcb in pairs(target_data) do
        TARGET = target
        if HOST == TARGET then
            NAME = TARGET
        else
            NAME = HOST.."_to_"..TARGET
        end

        emit("build", "compiler_for_"..HOST.."_to_"..TARGET, ":", "phony",
            "dependencies_for_"..RULE,
            "bin/"..NAME.."/init",
            "bin/"..NAME.."/tokeniser",
            "bin/"..NAME.."/parser",
            "bin/"..NAME.."/typechecker",
            "bin/"..NAME.."/backendify",
            "bin/"..NAME.."/blockifier",
            "bin/"..NAME.."/classifier",
            "bin/"..NAME.."/codegen",
            "bin/"..NAME.."/placer",
            "bin/"..NAME.."/emitter",
            "bin/"..NAME.."/iopshower",
            "bin/"..NAME.."/thingshower")
        nl()
        nl()

        targetcb()
        build_cowgol_programs()
    end
end

-- Build the bootstrap compiler tests.
for _, file in ipairs(posix.glob("tests/bootstrap/*.test.cow")) do
    bootstrap_test(file)
end

-- Build the CPU tests.
for _, file in ipairs(posix.glob("tests/cpu/*.test.cow")) do
    cpu_test(file)
end

build_c {
    "bbctube",
    "emu/bbctube/bbctube.c",
    "emu/bbctube/lib6502.c"
}

build_c {
    "mkdfs",
    "emu/mkdfs.c"
}

build_c {
    "mkadfs",
    "emu/mkadfs.c"
}
