/*
 *  Copyright (c) 2000-2022 Inria
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *  this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *  this list of conditions and the following disclaimer in the documentation
 *  and/or other materials provided with the distribution.
 *  * Neither the name of the ALICE Project-Team nor the names of its
 *  contributors may be used to endorse or promote products derived from this
 *  software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 *  Contact: Bruno Levy
 *
 *     https://www.inria.fr/fr/bruno-levy
 *
 *     Inria,
 *     Domaine de Voluceau,
 *     78150 Le Chesnay - Rocquencourt
 *     FRANCE
 *
 */

#include <geogram/basic/common.h>
#include <geogram/basic/process.h>
#include <geogram/basic/logger.h>
#include <geogram/basic/progress.h>
#include <geogram/basic/command_line.h>
#include <geogram/basic/file_system.h>
#include <geogram/basic/stopwatch.h>
#include <geogram/numerics/multi_precision.h>
#include <geogram/numerics/predicates.h>
#include <geogram/delaunay/delaunay.h>
#include <geogram/mesh/mesh_io.h>
#include <geogram/version.h>
#include <geogram/bibliography/bibliography.h>

#include <geogram/image/image.h>
#include <geogram/image/image_library.h>
#include <geogram/image/image_serializer_stb.h>
#include <geogram/image/image_serializer_xpm.h>
#include <geogram/image/image_serializer_pgm.h>

#include <sstream>
#include <iomanip>
#include <optional>

#ifdef GEO_OS_EMSCRIPTEN
#include <emscripten.h>
#endif

namespace GEO {

    namespace {

        /**
         * \brief A global object that manages initialization and
         *   termination of the Geogram library
         */
        struct GeogramLibSingleton {

            static std::optional<GeogramLibSingleton>& instance(int flags) {
                static std::optional<GeogramLibSingleton> instance(flags);
                return instance;
            }

            GeogramLibSingleton(int flags) {

                // When locale is set to non-us countries,
                // this may cause some problems when reading
                // floating-point numbers (some locale expect
                // a decimal ',' instead of a '.').
                // This restores the default behavior for
                // reading floating-point numbers.
#ifdef GEO_OS_UNIX
                if (flags & GEOGRAM_INSTALL_LOCALE) {
                    setenv("LC_NUMERIC","POSIX",1);
                }
#endif

#ifndef GEOGRAM_PSM
                Environment* env = Environment::instance();
                env->set_value("version", GEOGRAM_VERSION);
                env->set_value("release_date", VORPALINE_BUILD_DATE);
                env->set_value("SVN revision", VORPALINE_SVN_REVISION);
#endif
                FileSystem::initialize();
                Logger::initialize();
                Process::initialize(flags);
                // -fno-exceptions patch: Progress, Biblio, mesh_io_initialize,
                // ImageLibrary are in the SCsub exclude list. Skip their init.
                CmdLine::initialize();
                Stopwatch::initialize();
                PCK::initialize();
                Delaunay::initialize();

                // Clear lastest system error
                if (flags & GEOGRAM_INSTALL_ERRNO) {
                    errno = 0;
                }

                // -fno-exceptions patch: attribute type registration depends
                // on GEO::GeoFile::register_ascii_attribute_serializer from
                // basic/geofile.cpp, which we exclude. Delaunay doesn't need
                // attribute serialization, so skip.

                // NODEFS patch: see thirdparty/geogram/patches/. Upstream
                // mounts the node.js filesystem here via EM_ASM. Godot's web
                // export targets the browser, never node, so the mount is
                // dead — and the injected `NODEFS` reference is an undeclared
                // global that the closure compiler rejects under
                // use_closure_compiler=yes. The mount is dropped entirely.

                // -fno-exceptions patch: ImageLibrary is excluded.
            }

            ~GeogramLibSingleton() {

                if(
                    CmdLine::arg_is_declared("sys:stats") &&
                    CmdLine::get_arg_bool("sys:stats")
                ) {
                    Logger::div("System Statistics");
                    PCK::show_stats();
                    Process::show_stats();
                }

                PCK::terminate();
                // -fno-exceptions patch: ImageLibrary / Biblio / Progress
                // are excluded; their terminate() calls are dropped.
                Process::terminate();
                CmdLine::terminate();
                Logger::terminate();
                FileSystem::terminate();
                Environment::terminate();

            }
        };

    }

    void initialize(int flags) {
        GeogramLibSingleton::instance(flags);
    }

    void terminate() {
        GeogramLibSingleton::instance(GEOGRAM_INSTALL_NONE).reset();
    }
}
