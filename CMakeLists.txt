if(BUILD_APB_LIBS)
	set(APB_SOURCES
		APB_SpiFlashInterface.cpp)
else()
	set(APB_SOURCES
		)
endif()

add_library(embedded-utils STATIC
	${APB_SOURCES}
	CharacterDevice.cpp
	Logger.cpp
	QuadSPI_SpiFlashInterface.cpp
	SpiFlashInterfaceBase.cpp
	StringHelpers.cpp
	)

# TODO: only for stm32 targets?
target_include_directories(embedded-utils
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
	"$<TARGET_PROPERTY:stm32-cpp,INTERFACE_INCLUDE_DIRECTORIES>"
	"$<TARGET_PROPERTY:common-embedded-platform-core,INTERFACE_INCLUDE_DIRECTORIES>"
	)
