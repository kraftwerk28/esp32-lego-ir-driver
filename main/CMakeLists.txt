idf_component_register(SRCS main.c lego_encoder.c INCLUDE_DIRS ".")

target_compile_options(${COMPONENT_LIB} PRIVATE
	-Wno-format-overflow -Wno-nonnull -Wno-unused-function -Wno-unused-variable)

# target_link_libraries(${COMPONENT_LIB} -Wl,-zmuldefs)
