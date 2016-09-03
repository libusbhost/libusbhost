add_custom_target (doc
	COMMAND doxygen
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	COMMENT "output is generted in the ${CMAKE_SOURCE_DIR}/doc"
)
