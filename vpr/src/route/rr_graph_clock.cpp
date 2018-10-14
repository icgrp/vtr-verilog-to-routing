#include "rr_graph_clock.h"
#include "clock_network_types.h"

#include "globals.h"
#include "rr_graph.h"
#include "rr_graph2.h"
#include "rr_graph_area.h"
#include "rr_graph_util.h"

#include "vtr_assert.h"
#include "vtr_log.h"
#include "vpr_error.h"

void ClockRRGraph::create_and_append_clock_rr_graph(
        std::vector<t_segment_inf>& segment_inf,
        const float R_minW_nmos,
        const float R_minW_pmos)
{
    vtr::printf_info("Starting clock network routing resource graph generation...\n");
    clock_t begin = clock();

    auto& device_ctx = g_vpr_ctx.mutable_device();
    auto& clock_networks = device_ctx.clock_networks;
    auto& clock_routing = device_ctx.clock_connections;

    size_t clock_nodes_start_idx = device_ctx.rr_nodes.size();

    ClockRRGraph clock_graph = ClockRRGraph();
    clock_graph.create_clock_networks_wires(clock_networks);
    clock_graph.create_clock_networks_switches(clock_routing);

    // Reset fanin to account for newly added clock rr_nodes
    init_fan_in(device_ctx.rr_nodes, device_ctx.rr_nodes.size());

    clock_graph.add_rr_switches_and_map_to_nodes(clock_nodes_start_idx, R_minW_nmos, R_minW_pmos);

    // "Partition the rr graph edges for efficient access to configurable/non-configurable
    //  edge subsets. Must be done after RR switches have been allocated"
    partition_rr_graph_edges(device_ctx);

    //set_rr_node_cost_idx_based_on_seg_idx(segment_inf.size());

    float elapsed_time = (float) (clock() - begin) / CLOCKS_PER_SEC;
    vtr::printf_info("Building clock network resource graph took %g seconds\n", elapsed_time);
}

void ClockRRGraph::create_clock_networks_wires(
        std::vector<std::unique_ptr<ClockNetwork>>& clock_networks)
{
    // Add rr_nodes for each clock network wire
    for (auto& clock_network : clock_networks) {
        clock_network->create_rr_nodes_for_clock_network_wires(*this);
    }

    // Reduce the capacity of rr_nodes for performance
    auto& rr_nodes = g_vpr_ctx.mutable_device().rr_nodes;
    rr_nodes.shrink_to_fit();
}

void ClockRRGraph::create_clock_networks_switches(
        std::vector<std::unique_ptr<ClockConnection>>& clock_connections)
{
    for(auto& clock_connection: clock_connections) {
        clock_connection->create_switches(*this);
    }
}

void ClockRRGraph::add_rr_switches_and_map_to_nodes(
        size_t node_start_idx,
        const float R_minW_nmos,
        const float R_minW_pmos)
{
    auto& device_ctx = g_vpr_ctx.mutable_device();
    auto& rr_nodes = device_ctx.rr_nodes;

    // Check to see that clock nodes were sucessfully appended to rr_nodes
    VTR_ASSERT(rr_nodes.size() > node_start_idx);

    std::unordered_map<int, int> arch_switch_to_rr_switch;

    for(size_t node_idx = node_start_idx; node_idx < rr_nodes.size(); node_idx++) {
        auto& from_node = rr_nodes[node_idx];
        for(int edge_idx = 0; edge_idx < from_node.num_edges(); edge_idx++) {
            int arch_switch_idx = from_node.edge_switch(edge_idx);

            int rr_switch_idx;
            auto itter = arch_switch_to_rr_switch.find(arch_switch_idx);
            if(itter == arch_switch_to_rr_switch.end()) {
                rr_switch_idx = add_rr_switch_from_arch_switch_inf(
                    arch_switch_idx,
                    R_minW_nmos,
                    R_minW_pmos);
                arch_switch_to_rr_switch[arch_switch_idx] = rr_switch_idx;
            } else {
                rr_switch_idx = itter->second;
            }

            from_node.set_edge_switch(edge_idx, rr_switch_idx);
        }
    }

    device_ctx.rr_switch_inf.shrink_to_fit();
}

int ClockRRGraph::add_rr_switch_from_arch_switch_inf(
        int arch_switch_idx,
        const float R_minW_nmos,
        const float R_minW_pmos)
{
    auto& device_ctx = g_vpr_ctx.mutable_device();
    auto& rr_switch_inf = device_ctx.rr_switch_inf;
    auto& arch_switch_inf = device_ctx.arch_switch_inf;

    rr_switch_inf.emplace_back();
    int rr_switch_idx = rr_switch_inf.size() - 1;

    // TODO: Add support for non fixed Tdel based on fanin information
    VTR_ASSERT(arch_switch_inf[arch_switch_idx].fixed_Tdel());
    int fanin = UNDEFINED;

    rr_switch_inf[rr_switch_idx].set_type(arch_switch_inf[arch_switch_idx].type());
    rr_switch_inf[rr_switch_idx].R = arch_switch_inf[arch_switch_idx].R;
    rr_switch_inf[rr_switch_idx].Cin = arch_switch_inf[arch_switch_idx].Cin;
    rr_switch_inf[rr_switch_idx].Cout = arch_switch_inf[arch_switch_idx].Cout;
    rr_switch_inf[rr_switch_idx].Tdel = arch_switch_inf[arch_switch_idx].Tdel(fanin);
    rr_switch_inf[rr_switch_idx].mux_trans_size = arch_switch_inf[arch_switch_idx].mux_trans_size;

    if (device_ctx.arch_switch_inf[arch_switch_idx].buf_size_type == BufferSize::AUTO) {
        //Size based on resistance
        device_ctx.rr_switch_inf[rr_switch_idx].buf_size =
            trans_per_buf(device_ctx.arch_switch_inf[arch_switch_idx].R, R_minW_nmos, R_minW_pmos);
    } else {
        VTR_ASSERT(device_ctx.arch_switch_inf[arch_switch_idx].buf_size_type==BufferSize::ABSOLUTE);
        //Use the specified size
        device_ctx.rr_switch_inf[rr_switch_idx].buf_size =
            device_ctx.arch_switch_inf[arch_switch_idx].buf_size;
    }

    rr_switch_inf[rr_switch_idx].name = arch_switch_inf[arch_switch_idx].name;
    rr_switch_inf[rr_switch_idx].power_buffer_type =
        arch_switch_inf[arch_switch_idx].power_buffer_type;
    rr_switch_inf[rr_switch_idx].power_buffer_size =
        arch_switch_inf[arch_switch_idx].power_buffer_size;

    return rr_switch_idx;
}

void ClockRRGraph::add_switch_location(
        std::string clock_name,
        std::string switch_name,
        int x,
        int y,
        int node_index)
{
    // Note use of operator[] will automatically insert clock name if it doesn't exist
    clock_name_to_switch_points[clock_name].insert_switch_node_idx(switch_name, x, y, node_index);
}

void SwitchPoints::insert_switch_node_idx(std::string switch_name, int x, int y, int node_idx) {
    // Note use of operator[] will automatically insert switch name if it doesn't exit
    switch_name_to_switch_location[switch_name].insert_node_idx(x, y, node_idx);
}

void SwitchPoint::insert_node_idx(int x, int y, int node_idx) {
    // allocate 2d vector of grid size
    if (rr_node_indices.empty()) {
        auto& grid = g_vpr_ctx.device().grid;
        rr_node_indices.resize(grid.width());
        for (size_t i = 0; i < grid.width(); i++) {
            rr_node_indices[i].resize(grid.height());
        }
    }

    // insert node_idx at location
    rr_node_indices[x][y].push_back(node_idx);
    locations.insert({x,y});
}

std::vector<int> ClockRRGraph::get_rr_node_indices_at_switch_location(
    std::string clock_name,
    std::string switch_name,
    int x,
    int y) const
{
    auto itter = clock_name_to_switch_points.find(clock_name);

    // assert that clock name exists in map
    VTR_ASSERT(itter != clock_name_to_switch_points.end());

    auto& switch_points = itter->second;
    return switch_points.get_rr_node_indices_at_location(switch_name, x, y);
}

std::vector<int> SwitchPoints::get_rr_node_indices_at_location(
    std::string switch_name,
    int x,
    int y) const
{
    auto itter = switch_name_to_switch_location.find(switch_name);

    // assert that switch name exists in map
    VTR_ASSERT(itter != switch_name_to_switch_location.end());

    auto& switch_point = itter->second;
    std::vector<int> rr_node_indices = switch_point.get_rr_node_indices_at_location(x, y);
    return rr_node_indices;
}

std::vector<int> SwitchPoint::get_rr_node_indices_at_location(int x, int y) const {

    // assert that switch is connected to nodes at the location
    VTR_ASSERT(!rr_node_indices[x][y].empty());

    return rr_node_indices[x][y];
}

std::set<std::pair<int, int>> ClockRRGraph::get_switch_locations(
    std::string clock_name,
    std::string switch_name) const
{
    auto itter = clock_name_to_switch_points.find(clock_name);

    // assert that clock name exists in map
    VTR_ASSERT(itter != clock_name_to_switch_points.end());

    auto& switch_points = itter->second;
    return switch_points.get_switch_locations(switch_name);
}

std::set<std::pair<int, int>> SwitchPoints::get_switch_locations(std::string switch_name) const {

    auto itter = switch_name_to_switch_location.find(switch_name);

    // assert that switch name exists in map
    VTR_ASSERT(itter != switch_name_to_switch_location.end());

    auto& switch_point = itter->second;
    return switch_point.get_switch_locations();
}

std::set<std::pair<int, int>> SwitchPoint::get_switch_locations() const {

    // assert that switch is connected to nodes at the location
    VTR_ASSERT(!locations.empty());

    return locations;
}


int ClockRRGraph::get_and_increment_chanx_ptc_num() {

    auto& device_ctx = g_vpr_ctx.mutable_device();
    auto& grid = device_ctx.grid;
    auto* channel_width = &device_ctx.chan_width;

    int ptc_num = channel_width->x_max++;
    if (channel_width->x_max > channel_width->max) {
        channel_width->max = channel_width->x_max;
    }

    for (size_t i = 0; i < grid.height(); ++i) {
        device_ctx.chan_width.x_list[i]++;
    }

    return ptc_num;
}

int ClockRRGraph::get_and_increment_chany_ptc_num() {

    auto& device_ctx = g_vpr_ctx.mutable_device();
    auto& grid = device_ctx.grid;
    auto* channel_width = &device_ctx.chan_width;

    int ptc_num = channel_width->y_max++;
    if (channel_width->y_max > channel_width->max) {
        channel_width->max = channel_width->y_max;
    }

    for (size_t i = 0; i < grid.width(); ++i) {
        device_ctx.chan_width.y_list[i]++;
    }

    return ptc_num;
}


//void ClockRRGraph::create_star_model_network() {
//
//    vtr::printf_info("Creating a clock network in the form of a star model\n");
//
//    auto& device_ctx = g_vpr_ctx.mutable_device();
//    auto& rr_nodes = device_ctx.rr_nodes;
//    auto& rr_node_indices = device_ctx.rr_node_indices;
//
//    // 1) Create the clock source wire (located at the center of the chip)
//
//    // a) Find the center of the chip
//    auto& grid = device_ctx.grid;
//    auto x_mid_dim = grid.width() / 2;
//    auto y_mid_dim = grid.height() / 2;
//
//    // b) Create clock source wire node at at the center of the chip
//    rr_nodes.emplace_back();
//    auto clock_source_idx = rr_nodes.size() - 1; // last inserted node
//    rr_nodes[clock_source_idx].set_coordinates(x_mid_dim, x_mid_dim, y_mid_dim, y_mid_dim);
//    rr_nodes[clock_source_idx].set_type(CHANX);
//    rr_nodes[clock_source_idx].set_capacity(1);
//
//    // Find all I/O inpads and connect all I/O output pins to the clock source wire
//    // through a switch.
//    // Resize the rr_nodes array to 2x the number of I/O input pins
//    for (size_t i = 0; i < grid.width(); ++i) {
//        for (size_t j = 0; j < grid.height(); j++) {
//
//            auto type = grid[i][j].type;
//            auto width_offset = grid[i][j].width_offset;
//            auto height_offset = grid[i][j].height_offset;
//            for (e_side side : SIDES) {
//                //Don't connect pins which are not adjacent to channels around the perimeter
//                if (   (i == 0 && side != RIGHT)
//                    || (i == int(grid.width() - 1) && side != LEFT)
//                    || (j == 0 && side != TOP)
//                    || (j == int(grid.width() - 1) && side != BOTTOM)) {
//                    continue;
//                }
//
//                for (int pin_index = 0; pin_index < type->num_pins; pin_index++) {
//                    /* We only are working with opins so skip non-drivers */
//                    if (type->class_inf[type->pin_class[pin_index]].type != DRIVER) {
//                        continue;
//                    }
//
//                    /* Can't do anything if pin isn't at this location */
//                    if (0 == type->pinloc[width_offset][height_offset][side][pin_index]) {
//                        continue;
//                    }
//                    vtr::printf_info("%s \n",type->pb_type->name);
//                    if (strcmp(type->pb_type->name, "io") != 0) {
//                        continue;
//                    }
//
//                    auto node_index = get_rr_node_index(rr_node_indices, i, j, OPIN, pin_index, side);
//                    rr_nodes[node_index].add_edge(clock_source_idx, 0);
//                    vtr::printf_info("At %d,%d output pin node %d\n", i, j, node_index);
//
//
//                }
//                for (auto pin_index : type->get_clock_pins_indices()) {
//
//
//                    /* Can't do anything if pin isn't at this location */
//                    if (0 == type->pinloc[width_offset][height_offset][side][pin_index]) {
//                        continue;
//                    }
//
//
//                    auto node_index = get_rr_node_index(rr_node_indices, i, j, IPIN, pin_index, side);
//                    rr_nodes[clock_source_idx].add_edge(node_index, 1);
//                    vtr::printf_info("At %d,%d input pin node %d\n", i, j, node_index);
//
//                }
//
//            }
//
//            // Loop over ports
//                // Is the port class clock_source
//                    // Assert pin is an output (check)
//                    // find node using t_rr_node_index (check))
//                    // get_rr_node_index(rr_nodes, i, j, OPIN, side) (check)
//                    // create an edge to clock source wire (check)
//                // Is the port a clock
//                    // find pin using t_rr_node_indices (rr_type is a IPIN)
//                    // create an edge from the clock source wire to the
//        }
//    }
//
//    // Find all clock pins and connect them to the clock source wire
//
//
//    vtr::printf_info("Finished creating star model clock network\n");
//}

