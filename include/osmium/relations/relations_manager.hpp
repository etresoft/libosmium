#ifndef OSMIUM_RELATIONS_RELATIONS_MANAGER_HPP
#define OSMIUM_RELATIONS_RELATIONS_MANAGER_HPP

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

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

#include <osmium/handler.hpp>
#include <osmium/handler/check_order.hpp>
#include <osmium/memory/buffer.hpp>
#include <osmium/memory/callback_buffer.hpp>
#include <osmium/osm/item_type.hpp>
#include <osmium/osm/relation.hpp>
#include <osmium/osm/tag.hpp>
#include <osmium/osm/way.hpp>
#include <osmium/relations/manager_util.hpp>
#include <osmium/relations/members_database.hpp>
#include <osmium/relations/relations_database.hpp>
#include <osmium/storage/item_stash.hpp>
#include <osmium/tags/taglist.hpp>
#include <osmium/tags/tags_filter.hpp>

namespace osmium {

    namespace relations {

        /**
         * This is a base class of the RelationsManager class template. It
         * contains databases for the relations and the members that we need
         * to keep track of and handles the ouput buffer. Unlike the
         * RelationsManager class template this is a plain class.
         *
         * Usually it is better to use the RelationsManager class template
         * as a basis for your code, but you can also use this class if you
         * have special needs.
         */
        class RelationsManagerBase : public osmium::handler::Handler {

            // All relations and members we are interested in will be kept
            // in here.
            osmium::ItemStash m_stash;

            /// Database of all relations we are interested in.
            relations::RelationsDatabase m_relations_db;

            /// Databases of all members we are interested in.
            relations::MembersDatabase<osmium::Node>     m_member_nodes_db;
            relations::MembersDatabase<osmium::Way>      m_member_ways_db;
            relations::MembersDatabase<osmium::Relation> m_member_relations_db;

            /// Output buffer.
            osmium::memory::CallbackBuffer m_output;

        public:

            RelationsManagerBase() :
                m_stash(),
                m_relations_db(m_stash),
                m_member_nodes_db(m_stash, m_relations_db),
                m_member_ways_db(m_stash, m_relations_db),
                m_member_relations_db(m_stash, m_relations_db),
                m_output() {
            }

            /// Access the internal RelationsDatabase.
            osmium::relations::RelationsDatabase& relations_db() noexcept {
                return m_relations_db;
            }

            /// Access the internal database containing member nodes.
            osmium::relations::MembersDatabase<osmium::Node>& member_nodes_db() noexcept {
                return m_member_nodes_db;
            }

            /// Access the internal database containing member ways.
            osmium::relations::MembersDatabase<osmium::Way>& member_ways_db() noexcept {
                return m_member_ways_db;
            }

            /// Access the internal database containing member relations.
            osmium::relations::MembersDatabase<osmium::Relation>& member_relations_db() noexcept {
                return m_member_relations_db;
            }

            /**
             * Access the internal database containing members of the
             * specified type (non-const version of this function).
             *
             * @param type osmium::item_type::node, way, or relation.
             */
            relations::MembersDatabaseCommon& member_database(osmium::item_type type) {
                switch (type) {
                    case osmium::item_type::node:
                        return m_member_nodes_db;
                    case osmium::item_type::way:
                        return m_member_ways_db;
                    case osmium::item_type::relation:
                        return m_member_relations_db;
                    default:
                        break;
                }
                throw std::logic_error{"Should not be here."};
            }

            /**
             * Access the internal database containing members of the
             * specified type (const version of this function).
             *
             * @param type osmium::item_type::node, way, or relation.
             */
            const relations::MembersDatabaseCommon& member_database(osmium::item_type type) const {
                switch (type) {
                    case osmium::item_type::node:
                        return m_member_nodes_db;
                    case osmium::item_type::way:
                        return m_member_ways_db;
                    case osmium::item_type::relation:
                        return m_member_relations_db;
                    default:
                        break;
                }
                throw std::logic_error{"Should not be here."};
            }

            /**
             * Sort the members databases to prepare them for reading. Usually
             * this is called between the first and second pass reading through
             * an OSM data file.
             */
            void prepare_for_lookup() {
                m_member_nodes_db.prepare_for_lookup();
                m_member_ways_db.prepare_for_lookup();
                m_member_relations_db.prepare_for_lookup();
            }

            /**
             * Return the memory used by different components of the manager.
             */
            relations_manager_memory_usage used_memory() const noexcept {
                return {
                    m_relations_db.used_memory(),
                      m_member_nodes_db.used_memory()
                    + m_member_ways_db.used_memory()
                    + m_member_relations_db.used_memory(),
                    m_stash.used_memory()
                };
            }

            /// Access the output buffer.
            osmium::memory::Buffer& buffer() noexcept {
                return m_output.buffer();
            }

            /// Set the callback called when the output buffer is full.
            void set_callback(const std::function<void(osmium::memory::Buffer&&)>& callback) {
                m_output.set_callback(callback);
            }

            /// Flush the output buffer.
            void flush_output() {
                m_output.flush();
            }

            /// Flush the output buffer if it is full.
            void possibly_flush() {
                m_output.possibly_flush();
            }

            /// Return the contents of the output buffer.
            osmium::memory::Buffer read() {
                return m_output.read();
            }

        }; // class RelationsManagerBase

        /**
         * This is a base class for RelationManager classes. It keeps track of
         * all interesting relations and all interesting members of those
         * relations. When all members are available it calls code to handle
         * the completed relation.
         *
         * This class is intended as a base class for classes that handle
         * specific types of relations. Derive from this class, implement
         * the complete_relation() function and overwrite certain other
         * functions as needed.
         *
         * @tparam TManager The derived class (Uses CRTP).
         * @tparam TNodes Are we interested in member nodes?
         * @tparam TWays Are we interested in member ways?
         * @tparam TRelations Are we interested in member relations?
         *
         * @pre The Ids of all objects must be unique in the input data.
         */
        template <typename TManager, bool TNodes, bool TWays, bool TRelations>
        class RelationsManager : public RelationsManagerBase {

            using handler_pass2 = SecondPassHandlerWithCheckOrder<RelationsManager, TNodes, TWays, TRelations>;
            handler_pass2 m_handler_pass2;

            /**
             * This method is called from the first pass handler for every
             * relation in the input, to check whether it should be kept.
             *
             * Overwrite this method in a child class to only add relations
             * you are interested in, for instance depending on the type tag.
             * Storing relations takes a lot of memory, so it makes sense to
             * filter this as much as possible.
             */
            bool new_relation(const osmium::Relation& /*relation*/) const noexcept {
                return true;
            }

            /**
             * This method is called for every member of every relation that
             * will be kept. It should decide if the member is interesting or
             * not and return true or false to signal that. Only interesting
             * members are later added to the relation.
             *
             * Overwrite this method in a child class. In the
             * MultipolygonManager class this is used for instance to only
             * keep members of type way and ignore all others.
             */
            bool new_member(const osmium::Relation& /*relation*/, const osmium::RelationMember& /*member*/, std::size_t /*n*/) const noexcept {
                return true;
            }

            /**
             * This method is called for each complete relation, ie when
             * all members you have expressed interest in are available.
             *
             * You have to overwrite this in a child class.
             */
            void complete_relation(const osmium::Relation& /*relation*/) const noexcept {
            }

            /**
             * This method is called for all nodes during the second pass
             * before the relation member handling.
             *
             * Overwrite this method in a child class if you are interested
             * in this.
             */
            void before_node(const osmium::Node& /*node*/) const noexcept {
            }

            /**
             * This method is called for all nodes that are not a member of
             * any relation.
             *
             * Overwrite this method in a child class if you are interested
             * in this.
             */
            void node_not_in_any_relation(const osmium::Node& /*node*/) const noexcept {
            }

            /**
             * This method is called for all nodes during the second pass
             * after the relation member handling.
             *
             * Overwrite this method in a child class if you are interested
             * in this.
             */
            void after_node(const osmium::Node& /*node*/) const noexcept {
            }

            /**
             * This method is called for all ways during the second pass
             * before the relation member handling.
             *
             * Overwrite this method in a child class if you are interested
             * in this.
             */
            void before_way(const osmium::Way& /*way*/) const noexcept {
            }

            /**
             * This method is called for all ways that are not a member of
             * any relation.
             *
             * Overwrite this method in a child class if you are interested
             * in this.
             */
            void way_not_in_any_relation(const osmium::Way& /*way*/) const noexcept {
            }

            /**
             * This method is called for all ways during the second pass
             * after the relation member handling.
             *
             * Overwrite this method in a child class if you are interested
             * in this.
             */
            void after_way(const osmium::Way& /*way*/) const noexcept {
            }

            /**
             * This method is called for all relations during the second pass
             * before the relation member handling.
             *
             * Overwrite this method in a child class if you are interested
             * in this.
             */
            void before_relation(const osmium::Relation& /*relation*/) const noexcept {
            }

            /**
             * This method is called for all relations that are not a member of
             * any relation.
             *
             * Overwrite this method in a child class if you are interested
             * in this.
             */
            void relation_not_in_any_relation(const osmium::Relation& /*relation*/) const noexcept {
            }

            /**
             * This method is called for all relations during the second pass
             * after the relation member handling.
             *
             * Overwrite this method in a child class if you are interested
             * in this.
             */
            void after_relation(const osmium::Relation& /*relation*/) const noexcept {
            }

            void handle_complete_relation(RelationHandle& rel_handle) {
                static_cast<TManager*>(this)->complete_relation(*rel_handle);
                possibly_flush();

                for (const auto& member : rel_handle->members()) {
                    if (member.ref() != 0) {
                        member_database(member.type()).remove(member.ref(), rel_handle->id());
                    }
                }

                rel_handle.remove();
            }

        public:

            RelationsManager() :
                RelationsManagerBase(),
                m_handler_pass2(*this) {
            }

            /**
             * Return reference to second pass handler.
             */
            handler_pass2& handler(const std::function<void(osmium::memory::Buffer&&)>& callback = nullptr) {
                set_callback(callback);
                return m_handler_pass2;
            }

            /**
             * Add the specified relation to the list of relations we want to
             * build. This calls the new_relation() and new_member()
             * functions to actually decide what to keep.
             *
             * This member function is named relation() so the manager can
             * be used as a handler for the first pass through a data file.
             *
             * @param relation Relation we might want to build.
             */
            void relation(const osmium::Relation& relation) {
                if (static_cast<TManager*>(this)->new_relation(relation)) {
                    auto rel_handle = relations_db().add(relation);

                    std::size_t n = 0;
                    for (auto& member : rel_handle->members()) {
                        if (static_cast<TManager*>(this)->new_member(relation, member, n)) {
                            member_database(member.type()).track(rel_handle, member.ref(), n);
                        } else {
                            member.set_ref(0); // set member id to zero to indicate we are not interested
                        }
                        ++n;
                    }
                }
            }

            void handle_node(const osmium::Node& node) {
                before_node(node);
                const bool added = member_nodes_db().add(node, [this](RelationHandle& rel_handle) {
                    handle_complete_relation(rel_handle);
                });
                if (! added) {
                    node_not_in_any_relation(node);
                }
                after_node(node);
                possibly_flush();
            }

            void handle_way(const osmium::Way& way) {
                before_way(way);
                const bool added = member_ways_db().add(way, [this](RelationHandle& rel_handle) {
                    handle_complete_relation(rel_handle);
                });
                if (! added) {
                    way_not_in_any_relation(way);
                }
                after_way(way);
                possibly_flush();
            }

            void handle_relation(const osmium::Relation& relation) {
                before_relation(relation);
                const bool added = member_relations_db().add(relation, [this](RelationHandle& rel_handle) {
                    handle_complete_relation(rel_handle);
                });
                if (! added) {
                    relation_not_in_any_relation(relation);
                }
                after_relation(relation);
                possibly_flush();
            }

        }; // class RelationsManager

    } // namespace relations

} // namespace osmium

#endif // OSMIUM_RELATIONS_RELATIONS_MANAGER_HPP
