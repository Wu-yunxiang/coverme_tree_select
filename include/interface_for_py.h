#ifndef INTERFACE_FOR_PY_H
#define INTERFACE_FOR_PY_H

extern "C" {
    void initialize_runtime();
    int get_br_count();
    int get_arg_count();
    void set_target();
    int nExplored();
    void begin_self_phase();
    int finish_sample();
    void begin_base_phase();
    void begin_delta_phase();
    double get_r();
}

void update_sample();

#endif
