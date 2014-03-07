#ifndef OSMIUM_AREA_ASSEMBLER_HPP
#define OSMIUM_AREA_ASSEMBLER_HPP

/*

This file is part of Osmium (http://osmcode.org/osmium).

Copyright 2013,2014 Jochen Topf <jochen@topf.org> and others (see README).

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
#include <iostream>
#include <iterator>
#include <list>
#include <map>
#include <vector>

#include <osmium/memory/buffer.hpp>
#include <osmium/osm/area.hpp>
#include <osmium/osm/builder.hpp>
#include <osmium/osm/location.hpp>
#include <osmium/osm/ostream.hpp>
#include <osmium/osm/relation.hpp>

#include <osmium/area/segment.hpp>
#include <osmium/area/problem.hpp>
#include <osmium/area/detail/proto_ring.hpp>

namespace osmium {

    namespace area {

        using osmium::area::detail::ProtoRing;

        /**
         * Assembles area objects from multipolygon relations and their
         * members. This is called by the Collector object after all
         * members have been collected.
         */
        class Assembler {

            // List of problems found when assembling areas
            std::vector<Problem> m_problems {};

            // Enables list of problems to be kept
            bool m_remember_problems { false };

            // Enables debug output to stderr
            bool m_debug { false };

            bool is_below(const osmium::Location& loc, const NodeRefSegment& seg) const {
                double ax = seg.first().location().x();
                double bx = seg.second().location().x();
                double cx = loc.x();
                double ay = seg.first().location().y();
                double by = seg.second().location().y();
                double cy = loc.y();
                return ((bx - ax)*(cy - ay) - (by - ay)*(cx - ax)) <= 0;
            }

            /**
             * Find intersection between segments.
             *
             * @returns true if there are intersections.
             */
            bool find_intersections(std::vector<NodeRefSegment>& segments) {
                if (segments.begin() == segments.end()) {
                    return false;
                }

                bool found_intersections = false;

                for (auto it1 = segments.begin(); it1 != segments.end()-1; ++it1) {
                    const NodeRefSegment& s1 = *it1;
                    for (auto it2 = it1+1; it2 != segments.end(); ++it2) {
                        const NodeRefSegment& s2 = *it2;
                        if (s1 == s2) {
                            if (m_debug) {
                                std::cerr << "  found overlap on segment " << s1 << "\n";
                            }
                        } else {
                            if (outside_x_range(s2, s1)) {
                                break;
                            }
                            if (y_range_overlap(s1, s2)) {
                                osmium::Location intersection = calculate_intersection(s1, s2);
                                if (intersection) {
                                    found_intersections = true;
                                    if (m_debug) {
                                        std::cerr << "  segments " << s1 << " and " << s2 << " intersecting at " << intersection << "\n";
                                    }
                                    if (m_remember_problems) {
                                        m_problems.emplace_back(Problem(osmium::area::Problem::problem_type::intersection, osmium::NodeRef(0, intersection), s1, s2));
                                    }
                                }
                            }
                        }
                    }
                }

                return found_intersections;
            }

            void update_ring_link_in_segments(const ProtoRing* old_ring, ProtoRing* new_ring, std::vector<NodeRefSegment>& segments) {
                for (NodeRefSegment& segment : segments) {
                    if (segment.ring() == old_ring) {
                        segment.ring(new_ring);
                    }
                }
            }

            bool check_for_open_rings(const std::list<ProtoRing>& rings) {
                bool open_rings = false;

                for (auto& ring : rings) {
                    if (!ring.closed()) {
                        open_rings = true;
                        if (m_remember_problems) {
                            m_problems.emplace_back(Problem(osmium::area::Problem::problem_type::ring_not_closed, ring.first()));
                            m_problems.emplace_back(Problem(osmium::area::Problem::problem_type::ring_not_closed, ring.last()));
                        }
                    }
                }

                return open_rings;
            }

        public:

            Assembler() {
            }

            /**
             * Enable or disable debug output to stderr. This is for Osmium
             * developers only.
             */
            void enable_debug_output(bool debug=true) {
                m_debug = debug;
            }

            /**
             * Enable or disable collection of problems in the input data.
             * If this is enabled the assembler will keep a list of all
             * problems found (such as self-intersections and unclosed rings).
             * This creates some overhead so it is disabled by default.
             */
            void remember_problems(bool remember=true) {
                m_remember_problems = remember;
            }

            /**
             * Clear the list of problems that have been found.
             */
            void clear_problems() {
                m_problems.clear();
            }

            /**
             * Get the list of problems found so far in the input data.
             */
            const std::vector<Problem>& problems() const {
                return m_problems;
            }

            void operator()(const osmium::Relation& relation, std::vector<size_t>& members, const osmium::memory::Buffer& in_buffer, osmium::memory::Buffer& out_buffer) {
                // First we extract all segments from all ways that make up this
                // multipolygon relation. The segments all have their smaller
                // coordinate at the beginning of the segment. Smaller, in this
                // case, means smaller x coordinate, and if they are the same
                // smaller y coordinate.
                std::vector<NodeRefSegment> segments;

                for (size_t offset : members) {
                    const osmium::Way& way = in_buffer.get<const osmium::Way>(offset);
                    osmium::NodeRef last_nr;
                    for (osmium::NodeRef nr : way.nodes()) {
                        if (last_nr.location() && last_nr != nr) {
                            segments.push_back(NodeRefSegment(last_nr, nr));
                        }
                        last_nr = nr;
                    }
                }

                if (m_debug) {
                    std::cerr << "\nBuild relation id()=" << relation.id() << " members.size()=" << members.size() << " segments.size()=" << segments.size() << "\n";
                }

                // Now all of these segments will be sorted. Again, smaller, in
                // this case, means smaller x coordinate, and if they are the
                // same smaller y coordinate.
                std::sort(segments.begin(), segments.end());

                // remove empty segments
/*                segments.erase(std::remove_if(segments.begin(), segments.end(), [](osmium::UndirectedSegment& segment) {
                    return segment.first() == segment.second();
                }));*/

                // Find duplicate segments (ie same start and end point) and
                // remove them. This will always remove pairs of the same
                // segment. So if there are three, for instance, two will be
                // removed and one will be left.
                while (true) {
                    std::vector<NodeRefSegment>::iterator found = std::adjacent_find(segments.begin(), segments.end());
                    if (found == segments.end()) {
                        break;
                    }
                    if (m_debug) {
                        std::cerr << "  erase duplicate segment: " << *found << "\n";
                    }
                    segments.erase(found, found+2);
                }

                // Now create the Area object and add the attributes and tags
                // from the relation.
                osmium::osm::AreaBuilder builder(out_buffer);
                osmium::Area& area = builder.object();
                area.id(relation.id() * 2 + 1);
                area.version(relation.version());
                area.changeset(relation.changeset());
                area.timestamp(relation.timestamp());
                area.visible(relation.visible());
                area.uid(relation.uid());

                builder.add_user(relation.user());

                {
                    osmium::osm::TagListBuilder tl_builder(out_buffer, &builder);
                    for (const osmium::Tag& tag : relation.tags()) {
                        tl_builder.add_tag(tag.key(), tag.value());
                    }
                }

                // From now on we have an area object without any rings in it.
                // Areas without rings are "defined" to be invalid. We can commit
                // this area at any time and the caller of the assembler will see
                // the invalid area. Or we can later add the rings and make a valid
                // area out of it.

                // Now we look for segments crossing each other. If there are
                // any, the multipolygon is invalid.
                // In the future this could be improved by trying to fix those
                // cases.
                if (find_intersections(segments)) {
                    out_buffer.commit();
                    return;
                }


                // Now iterator over all segments and add them to rings
                // until there are no segments left.
                std::list<ProtoRing> rings;
                for (auto it = segments.begin(); it != segments.end(); ++it) {
                    auto& segment = *it;

                    if (m_debug) {
                        std::cerr << "  check segment " << segment << "\n";
                    }

                    int n=0;
                    for (auto& ring : rings) {
                        if (m_debug) {
                            std::cerr << "    check against ring " << n << " " << ring << "\n";
                        }
                        if (!ring.closed()) {
                            if (ring.last() == segment.first() ) {
                                if (m_debug) {
                                    std::cerr << "      match\n";
                                }
                                segment.ring(&ring);
                                ring.add_location_end(segment.second());
                                ProtoRing* pr = combine_rings_end(ring, rings, m_debug);
                                update_ring_link_in_segments(pr, &ring, segments);
                                goto next_segment;
                            }
                            if (ring.last() == segment.second() ) {
                                if (m_debug) {
                                    std::cerr << "      match\n";
                                }
                                segment.ring(&ring);
                                ring.add_location_end(segment.first());
                                ProtoRing* pr = combine_rings_end(ring, rings, m_debug);
                                update_ring_link_in_segments(pr, &ring, segments);
                                goto next_segment;
                            }
                            if (ring.first() == segment.first() ) {
                                if (m_debug) {
                                    std::cerr << "      match\n";
                                }
                                segment.ring(&ring);
                                ring.add_location_start(segment.second());
                                ProtoRing* pr = combine_rings_start(ring, rings, m_debug);
                                update_ring_link_in_segments(pr, &ring, segments);
                                goto next_segment;
                            }
                            if (ring.first() == segment.second() ) {
                                if (m_debug) {
                                    std::cerr << "      match\n";
                                }
                                segment.ring(&ring);
                                ring.add_location_start(segment.first());
                                ProtoRing* pr = combine_rings_start(ring, rings, m_debug);
                                update_ring_link_in_segments(pr, &ring, segments);
                                goto next_segment;
                            }
                        } else {
                            if (m_debug) {
                                std::cerr << "      ring CLOSED\n";
                            }
                        }

                        ++n;
                    }

                    {
                        if (m_debug) {
                            std::cerr << "    new ring for segment " << segment << "\n";
                        }

                        bool cw = true;

                        if (it != segments.begin()) {
                            osmium::Location loc = segment.first().location();
                            if (m_debug) {
                                std::cerr << "      compare against id=" << segment.first().ref() << " lat()=" << loc.lat() << "\n";
                            }
                            for (auto oit = it-1; oit != segments.begin()-1; --oit) {
                                if (m_debug) {
                                    std::cerr << "      seg=" << *oit << "\n";
                                }
                                std::pair<int32_t, int32_t> mm = std::minmax(oit->first().location().y(), oit->second().location().y());
                                if (mm.first <= loc.y() && mm.second >= loc.y()) {
                                    if (m_debug) {
                                        std::cerr << "        in range\n";
                                    }
                                    if (oit->first().location().x() <= loc.x() &&
                                        oit->second().location().x() <= loc.x()) {
                                        cw = !oit->cw();
                                        segment.left_segment(&*oit);
                                        break;
                                    }
                                    if (is_below(loc, *oit)) { // XXX
                                        cw = !oit->cw();
                                        segment.left_segment(&*oit);
                                        break;
                                    }
                                }
                            }
                        }

                        if (m_debug) {
                            std::cerr << "      is " << (cw ? "cw" : "ccw") << "\n";
                        }

                        segment.cw(cw);
                        rings.emplace_back(ProtoRing(segment));
                        segment.ring(&rings.back());
                    }

                    next_segment:
                        ;

                }

                if (m_debug) {
                    std::cerr << "  Rings:\n";
                    for (auto& ring : rings) {
                        std::cerr << "    " << ring;
                        if (ring.closed()) {
                            std::cerr << " (closed)";
                        }
                        std::cerr << "\n";
                    }
                }

                if (check_for_open_rings(rings)) {
                    if (m_debug) {
                        std::cerr << "  not all rings are closed\n";
                    }
                    out_buffer.commit();
                    return;
                }

                if (m_debug) {
                    std::cerr << "  Find inner/outer...\n";
                }

                // Find inner rings to each outer ring.
                std::vector<ProtoRing*> outer_rings;
                for (auto& ring : rings) {
                    if (ring.is_outer()) {
                        if (m_debug) {
                            std::cerr << "    Outer: " << ring << "\n";
                        }
                        outer_rings.push_back(&ring);
                    } else {
                        if (m_debug) {
                            std::cerr << "    Inner: " << ring << "\n";
                        }
                        ProtoRing* outer = ring.find_outer(m_debug);
                        if (outer) {
                            outer->add_inner_ring(&ring);
                        } else {
                            if (m_debug) {
                                std::cerr << "    something bad happened\n";
                            }
                            out_buffer.commit();
                            return;
                        }
                    }
                }

                // Append each outer ring together with its inner rings to the
                // area in the buffer.
                for (const ProtoRing* ring : outer_rings) {
                    if (m_debug) {
                        std::cerr << "    ring " << *ring << " is outer\n";
                    }
                    osmium::osm::OuterRingBuilder ring_builder(out_buffer, &builder);
                    for (auto& node_ref : ring->nodes()) {
                        ring_builder.add_node_ref(node_ref);
                    }
                    for (ProtoRing* inner : ring->inner_rings()) {
                        osmium::osm::InnerRingBuilder ring_builder(out_buffer, &builder);
                        for (auto& node_ref : inner->nodes()) {
                            ring_builder.add_node_ref(node_ref);
                        }
                    }
                    out_buffer.commit();
                }
            }

        }; // class Assembler

    } // namespace area

} // namespace osmium

#endif // OSMIUM_AREA_ASSEMBLER_HPP
