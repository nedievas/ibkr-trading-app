# C++ TWS API Client
#
# Copyright (C) 2013-2026  Interactive Brokers LLC
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.

file(GLOB SOURCES "*.cpp")
file(GLOB HEADERS "*.h")
set(twsapi_VERSION 9.79.02)
add_library(twsapi SHARED ${SOURCES})
# On Windows (including MinGW), link against Winsock 2
if( WIN32 )
    target_link_libraries( twsapi
        PUBLIC
        ws2_32
    )
endif()
if( MSVC )
    target_compile_options( twsapi PRIVATE /W3 )
else()
    target_compile_options( twsapi PRIVATE -Wall -Wextra -pedantic -Wno-unused-parameter )
endif()
set_target_properties(twsapi
    PROPERTIES
    PREFIX "lib"
    DEBUG_POSTFIX "d"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    PUBLIC_HEADER "${HEADERS}"
)
install(TARGETS twsapi EXPORT twsapi-targets PUBLIC_HEADER DESTINATION include/twsapi LIBRARY DESTINATION lib)