# embed_file(<input> <output.cpp> <symbol>)
# Writes a .cpp defining:  extern const unsigned char <symbol>[];  extern const unsigned int <symbol>_size;
function(embed_file input output symbol)
  file(READ "${input}" hex HEX)
  string(LENGTH "${hex}" hexlen)
  math(EXPR size "${hexlen} / 2")
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
  string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n" bytes "${bytes}")
  file(WRITE "${output}.tmp"
    "// Generated from ${symbol}; do not edit.\n"
    "extern const unsigned char ${symbol}[] = {\n${bytes}\n};\n"
    "extern const unsigned int ${symbol}_size = ${size};\n")
  file(COPY_FILE "${output}.tmp" "${output}" ONLY_IF_DIFFERENT)
endfunction()
