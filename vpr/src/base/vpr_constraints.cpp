#include "vpr_constraints.h"

void VprConstraints::add_constrained_atom(const AtomBlockId blk_id, const PartitionId part_id) {
    constrained_atoms.insert({blk_id, part_id});

    //std::unordered_map<AtomBlockId, PartitionId>::const_iterator got = constrained_atoms.find(blk_id);
    auto got = constrained_atoms.find(blk_id);

    if (got == constrained_atoms.end()) {
        constrained_atoms.insert({blk_id, part_id});
    } else {
        got->second = part_id;
    }
}

PartitionId VprConstraints::get_atom_partition(AtomBlockId blk_id) {
    PartitionId part_id;

    //std::unordered_map<AtomBlockId, PartitionId>::const_iterator got = constrained_atoms.find(blk_id);
    auto got = constrained_atoms.find(blk_id);

    if (got == constrained_atoms.end()) {
        return part_id = PartitionId::INVALID();
    } else {
        return got->second;
    }
}

void VprConstraints::add_partition(Partition part) {
    partitions.push_back(part);
}

Partition VprConstraints::get_partition(PartitionId part_id) {
    return partitions[part_id];
}

std::vector<AtomBlockId> VprConstraints::get_part_atoms(PartitionId part_id) {
    std::vector<AtomBlockId> part_atoms;

    for (auto& it : constrained_atoms) {
        if (it.second == part_id) {
            part_atoms.push_back(it.first);
        }
    }

    return part_atoms;
}
