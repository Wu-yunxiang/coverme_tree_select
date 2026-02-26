#ifndef INTERFACE_FOR_PY_H
#define INTERFACE_FOR_PY_H

extern "C" {
    struct TargetAndSeed {
        int targetId;
        int seedId;
    };

    void initialize_runtime();
    int get_br_count();
    int get_arg_count();
    void warmup_target(int targetNode);
    TargetAndSeed pop_queue_target();
    int nExplored();
    void begin_self_phase();
    int finish_sample();
    void begin_base_phase();
    void begin_delta_phase();
    void update_queue();
    double get_r();
}

void update_sample();

#endif
