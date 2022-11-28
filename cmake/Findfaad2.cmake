cmake_minimum_required(VERSION 3.10)
project(faad2)

set(FAAD_SRC_DIR ${CMAKE_SOURCE_DIR}/vendor/faad2/libfaad)
set(FAAD_HEADER_DIR ${CMAKE_SOURCE_DIR}/vendor/faad2/include)

add_library(faad2 SHARED
    ${FAAD_SRC_DIR}/bits.c 
    ${FAAD_SRC_DIR}/cfft.c 
    ${FAAD_SRC_DIR}/decoder.c 
    ${FAAD_SRC_DIR}/drc.c 
    ${FAAD_SRC_DIR}/drm_dec.c 
    ${FAAD_SRC_DIR}/error.c 
    ${FAAD_SRC_DIR}/filtbank.c
    ${FAAD_SRC_DIR}/ic_predict.c 
    ${FAAD_SRC_DIR}/is.c 
    ${FAAD_SRC_DIR}/lt_predict.c 
    ${FAAD_SRC_DIR}/mdct.c 
    ${FAAD_SRC_DIR}/mp4.c 
    ${FAAD_SRC_DIR}/ms.c 
    ${FAAD_SRC_DIR}/output.c 
    ${FAAD_SRC_DIR}/pns.c
    ${FAAD_SRC_DIR}/ps_dec.c
    ${FAAD_SRC_DIR}/ps_syntax.c
    ${FAAD_SRC_DIR}/pulse.c
    ${FAAD_SRC_DIR}/specrec.c
    ${FAAD_SRC_DIR}/syntax.c
    ${FAAD_SRC_DIR}/tns.c
    ${FAAD_SRC_DIR}/hcr.c
    ${FAAD_SRC_DIR}/huffman.c
    ${FAAD_SRC_DIR}/rvlc.c
    ${FAAD_SRC_DIR}/ssr.c
    ${FAAD_SRC_DIR}/ssr_fb.c
    ${FAAD_SRC_DIR}/ssr_ipqf.c
    ${FAAD_SRC_DIR}/common.c
    ${FAAD_SRC_DIR}/sbr_dct.c
    ${FAAD_SRC_DIR}/sbr_e_nf.c
    ${FAAD_SRC_DIR}/sbr_fbt.c
    ${FAAD_SRC_DIR}/sbr_hfadj.c
    ${FAAD_SRC_DIR}/sbr_hfgen.c
    ${FAAD_SRC_DIR}/sbr_huff.c
    ${FAAD_SRC_DIR}/sbr_qmf.c
    ${FAAD_SRC_DIR}/sbr_syntax.c
    ${FAAD_SRC_DIR}/sbr_tf_grid.c
    ${FAAD_SRC_DIR}/sbr_dec.c
    ${FAAD_SRC_DIR}/analysis.h
    ${FAAD_SRC_DIR}/bits.h
    ${FAAD_SRC_DIR}/cfft.h
    ${FAAD_SRC_DIR}/cfft_tab.h
    ${FAAD_SRC_DIR}/common.h
    ${FAAD_SRC_DIR}/drc.h
    ${FAAD_SRC_DIR}/drm_dec.h
    ${FAAD_SRC_DIR}/error.h
    ${FAAD_SRC_DIR}/fixed.h
    ${FAAD_SRC_DIR}/filtbank.h
    ${FAAD_SRC_DIR}/huffman.h
    ${FAAD_SRC_DIR}/ic_predict.h
    ${FAAD_SRC_DIR}/iq_table.h
    ${FAAD_SRC_DIR}/is.h
    ${FAAD_SRC_DIR}/kbd_win.h
    ${FAAD_SRC_DIR}/lt_predict.h
    ${FAAD_SRC_DIR}/mdct.h
    ${FAAD_SRC_DIR}/mdct_tab.h
    ${FAAD_SRC_DIR}/mp4.h
    ${FAAD_SRC_DIR}/ms.h
    ${FAAD_SRC_DIR}/output.h
    ${FAAD_SRC_DIR}/pns.h
    ${FAAD_SRC_DIR}/ps_dec.h
    ${FAAD_SRC_DIR}/ps_tables.h
    ${FAAD_SRC_DIR}/pulse.h
    ${FAAD_SRC_DIR}/rvlc.h
    ${FAAD_SRC_DIR}/sbr_dct.h
    ${FAAD_SRC_DIR}/sbr_dec.h
    ${FAAD_SRC_DIR}/sbr_e_nf.h
    ${FAAD_SRC_DIR}/sbr_fbt.h
    ${FAAD_SRC_DIR}/sbr_hfadj.h
    ${FAAD_SRC_DIR}/sbr_hfgen.h
    ${FAAD_SRC_DIR}/sbr_huff.h
    ${FAAD_SRC_DIR}/sbr_noise.h
    ${FAAD_SRC_DIR}/sbr_qmf.h
    ${FAAD_SRC_DIR}/sbr_syntax.h
    ${FAAD_SRC_DIR}/sbr_tf_grid.h
    ${FAAD_SRC_DIR}/sine_win.h
    ${FAAD_SRC_DIR}/specrec.h
    ${FAAD_SRC_DIR}/ssr.h
    ${FAAD_SRC_DIR}/ssr_fb.h
    ${FAAD_SRC_DIR}/ssr_ipqf.h
    ${FAAD_SRC_DIR}/ssr_win.h
    ${FAAD_SRC_DIR}/syntax.h
    ${FAAD_SRC_DIR}/structs.h
    ${FAAD_SRC_DIR}/tns.h
    ${FAAD_SRC_DIR}/sbr_qmf_c.h
    ${FAAD_SRC_DIR}/codebook/hcb.h
    ${FAAD_SRC_DIR}/codebook/hcb_1.h
    ${FAAD_SRC_DIR}/codebook/hcb_2.h
    ${FAAD_SRC_DIR}/codebook/hcb_3.h
    ${FAAD_SRC_DIR}/codebook/hcb_4.h
    ${FAAD_SRC_DIR}/codebook/hcb_5.h
    ${FAAD_SRC_DIR}/codebook/hcb_6.h
    ${FAAD_SRC_DIR}/codebook/hcb_7.h
    ${FAAD_SRC_DIR}/codebook/hcb_8.h
    ${FAAD_SRC_DIR}/codebook/hcb_9.h
    ${FAAD_SRC_DIR}/codebook/hcb_10.h
    ${FAAD_SRC_DIR}/codebook/hcb_11.h
    ${FAAD_SRC_DIR}/codebook/hcb_sf.h)

target_include_directories(faad2 PRIVATE ${FAAD_SRC_DIR})
target_include_directories(faad2 PUBLIC ${FAAD_HEADER_DIR})
set_target_properties(faad2 PROPERTIES CXX_STANDARD 17)

if(WIN32)
# win32_ver.h is required to set the package version string
configure_file(cmake/faad2_win32_ver.h.in ${FAAD_SRC_DIR}/win32_ver.h)
target_compile_definitions(faad2 PRIVATE
    HAVE_STDINT_H
    HAVE_STRING_H
    HAVE_MEMCPY)
else()
target_compile_definitions(faad2 PRIVATE
    PACKAGE_VERSION="libfaad2"
    HAVE_STDINT_H
    HAVE_STRING_H
    HAVE_MEMCPY)
endif()
