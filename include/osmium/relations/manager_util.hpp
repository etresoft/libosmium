#ifndef OSMIUM_RELATIONS_MANAGER_UTIL_HPP
#define OSMIUM_RELATIONS_MANAGER_UTIL_HPP

/*

This file is part of Osmium (http://osmcode.org/libosmium).

Copyright 2013-2017 Jochen Topf <jochen@topf.org> and others (see README).

Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

*/

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iomanip>
#include <utility>

#include <osmium/fwd.hpp>
#include <osmium/handler.hpp>
#include <osmium/handler/check_order.hpp>
#include <osmium/io/reader.hpp>
#include <osmium/relations/relations_database.hpp>
#include <osmium/util/progress_bar.hpp>
#include <osmium/visitor.hpp>

namespace osmium {

    namespace relations {

        /**
         * This is a handler class used for the second pass of relation
         * managers. An object of this class is instantiated as a member
         * of the Manager and used to re-direct all calls to the handler
         * to the "parent" manager.
         *
         * @tparam TManager The manager we want to call functions on.
         * @tparam TNodes Are we interested in member nodes?
         * @tparam TWays Are we interested in member ways?
         * @tparam TRelations Are we interested in member relations?
         */
        template <typename TManager, bool TNodes = true, bool TWays = true, bool TRelations = true>
        class SecondPassHandlerWithCheckOrder : public osmium::handler::Handler {

            osmium::handler::CheckOrder m_check_order;
            TManager& m_manager;

        public:

            explicit SecondPassHandlerWithCheckOrder(TManager& manager) noexcept :
                m_manager(manager) {
            }

            /**
             * Overwrites the function in the handler parent class.
             */
            void node(const osmium::Node& node) {
                if (TNodes) {
                    m_check_order.node(node);
                    m_manager.handle_node(node);
                }
            }

            /**
             * Overwrites the function in the handler parent class.
             */
            void way(const osmium::Way& way) {
                if (TWays) {
                    m_check_order.way(way);
                    m_manager.handle_way(way);
                }
            }

            /**
             * Overwrites the function in the handler parent class.
             */
            void relation(const osmium::Relation& relation) {
                if (TRelations) {
                    m_check_order.relation(relation);
                    m_manager.handle_relation(relation);
                }
            }

            /**
             * Overwrites the function in the handler parent class.
             *
             * Calls the flush_output() function on the manager.
             */
            void flush() {
                m_manager.flush_output();
            }

        }; // class SecondPassHandlerWithCheckOrder

        /**
         * Read relations from file and feed them into all the managers
         * specified as parameters. Opens an osmium::io::Reader internally
         * with the file parameter.
         *
         * After the file is read, the prepare_for_lookup() function is called
         * on all the managers making them ready for querying the data they
         * have stored.
         *
         * @tparam TManager Any number of relation manager types.
         * @param file The file that should be opened with an osmium::io::Reader.
         * @param managers Relation managers we want the relations to be sent
         *                 to.
         */
        template <typename ...TManager>
        void read_relations(const osmium::io::File& file, TManager&& ...managers) {
            static_assert(sizeof...(TManager) > 0, "Need at least one manager as parameter.");
            osmium::io::Reader reader{file, osmium::osm_entity_bits::relation};
            osmium::apply(reader, std::forward<TManager>(managers)...);
            reader.close();
            (void)std::initializer_list<int>{
                (std::forward<TManager>(managers).prepare_for_lookup(), 0)...
            };
        }

        /**
         * Read relations from file and feed them into all the managers
         * specified as parameters. Opens an osmium::io::Reader internally
         * with the file parameter.
         *
         * After the file is read, the prepare_for_lookup() function is called
         * on all the managers making them ready for querying the data they
         * have stored.
         *
         * @tparam TManager Any number of relation manager types.
         * @param progress_bar Reference to osmium::ProgressBar object that
         *                     will be updated while reading the data.
         * @param file The file that should be opened with an osmium::io::Reader.
         * @param managers Relation managers we want the relations to be sent
         *                 to.
         */
        template <typename ...TManager>
        void read_relations(osmium::ProgressBar& progress_bar, const osmium::io::File& file, TManager&& ...managers) {
            static_assert(sizeof...(TManager) > 0, "Need at least one manager as parameter.");
            osmium::io::Reader reader{file, osmium::osm_entity_bits::relation};
            while (auto buffer = reader.read()) {
                progress_bar.update(reader.offset());
                osmium::apply(buffer, std::forward<TManager>(managers)...);
            }
            reader.close();
            (void)std::initializer_list<int>{
                (std::forward<TManager>(managers).prepare_for_lookup(), 0)...
            };
            progress_bar.file_done(file.size());
        }

        /**
         * Struct for memory usage numbers returned by various relations
         * managers from the used_memory() function.
         */
        struct relations_manager_memory_usage {
            std::size_t relations_db;
            std::size_t members_db;
            std::size_t stash;
        };

        /**
         * Prints relations managers memory usage numbers to the specified
         * stream.
         *
         * @tparam TStream Output stream type (like std::cout, std::cerr, or
         *                 osmium::util::VerboseOutput).
         * @param stream Reference to stream where the output should go.
         * @param mu Memory usage data as returned by the used_memory()
         *                  functions of various relations managers.
         */
        template <typename TStream>
        void print_used_memory(TStream& stream, const relations_manager_memory_usage& mu) {
            const auto total = mu.relations_db + mu.members_db + mu.stash;

            stream << "  relations: " << std::setw(8) << (mu.relations_db / 1024) << " kB\n"
                   << "  members:   " << std::setw(8) << (mu.members_db   / 1024) << " kB\n"
                   << "  stash:     " << std::setw(8) << (mu.stash        / 1024) << " kB\n"
                   << "  total:     " << std::setw(8) << (total           / 1024) << " kB\n"
                   << "  ======================\n";
        }

    } // namespace relations

} // namespace osmium

#endif // OSMIUM_RELATIONS_MANAGER_UTIL_HPP
