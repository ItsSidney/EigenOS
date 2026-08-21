import sys

with open("build.sh", "r") as f:
    lines = f.readlines()

new_lines = []
in_folders = False

for line in lines:
    if "for f in src/user/lib/tinygl/*.c; do" in line:
        new_lines.append(line)
        continue
    
    if "    # Single-file apps under src/user/apps/" in line:
        new_lines.append("""
    # FreeType
    local FT_SRCS="base/ftsystem.c base/ftinit.c base/ftdebug.c base/ftbase.c base/ftbbox.c base/ftglyph.c base/ftbdf.c base/ftbitmap.c truetype/truetype.c sfnt/sfnt.c smooth/smooth.c raster/raster.c psnames/psnames.c psaux/psaux.c pshinter/pshinter.c"
    for f in $FT_SRCS; do
        if [ -f "src/user/lib/freetype/src/$f" ]; then
            local base=$(basename "$f" .c)
            echo "  CC lib/freetype/src/$f -> bin/obj/user/lib/ft_$base.o"
            $UCC $UCFLAGS -DFT2_BUILD_LIBRARY -Isrc/user/lib/freetype/include "src/user/lib/freetype/src/$f" -o "bin/obj/user/lib/ft_$base.o"
        fi
    done

    # ImGui
    for f in src/user/lib/imgui/*.cpp src/user/lib/imgui/backends/*.cpp; do
        if [ -f "$f" ]; then
            local base=$(basename "$f" .cpp)
            echo "  CXX lib/imgui/$base.cpp -> bin/obj/user/lib/ig_$base.o"
            g++ $UCFLAGS -fno-exceptions -fno-rtti -Isrc/user/lib/imgui -Isrc/user/lib/imgui/backends "$f" -o "bin/obj/user/lib/ig_$base.o"
        fi
    done

""")
        new_lines.append(line)
        continue

    if "for s in $SRCS; do" in line:
        new_lines.append(line)
        in_folders = True
        continue
        
    if in_folders and "            $UCC $UCFLAGS $inc_flags $FLAGS \"$src\" -o \"$obj\" \\" in line:
        new_lines.append("""
            if [[ "$src" == *.cpp ]]; then
                echo "  CXX ${dir#src/user/apps/}/$s"
                g++ $UCFLAGS -fno-exceptions -fno-rtti $inc_flags $FLAGS "$src" -o "$obj" || { echo "[FOLDER] build failed on $src"; return 1; }
            else
                $UCC $UCFLAGS $inc_flags $FLAGS "$src" -o "$obj" || { echo "[FOLDER] build failed on $src"; return 1; }
            fi
""")
        continue
    if in_folders and "                || { echo \"[FOLDER] build failed on $src\"; return 1; }" in line:
        continue # skip the rest of the old UCC call
    if in_folders and "            OBJS=\"$OBJS $obj\"" in line:
        in_folders = False
        new_lines.append(line)
        continue

    if "        $LD $ULDFLAGS -o \"bin/userapp/$NAME.elf\" $OBJS \\" in line:
        new_lines.append(line)
        continue
    if "            bin/obj/user/lib/*.o \\" in line:
        new_lines.append("            bin/obj/user/lib/*.o \\\n")
        continue

    new_lines.append(line)

with open("build.sh", "w") as f:
    f.writelines(new_lines)

