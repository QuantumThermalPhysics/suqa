#pragma once
#include "Rand.hpp"
#include "io.hpp"
#include "suqa.cuh"
#include "complex_defines.cuh"


// defined in src/evolution.cpp
void cevolution(ComplexVec& state, const double& t, const int& n, const uint& q_control, const std::vector<uint>& qstate);

void fill_meas_cache(const std::vector<uint>& bm_states, const std::string opstem);

void apply_measure_rotation(ComplexVec& state);
void apply_measure_antirotation(ComplexVec& state);
double get_meas_opvals(const uint& creg_vals);

void apply_C(ComplexVec& state, const std::vector<uint>& bm_states, const uint &Ci);

void apply_C_inverse(ComplexVec& state, const std::vector<uint>& bm_states, const uint &Ci);

std::vector<double> get_C_weigthsums();
// end defs

namespace qms{

uint state_qbits;
uint state_levels;
uint ene_qbits;
uint ene_levels;
uint nqubits;
uint Dim;
uint max_reverse_attempts;
uint metro_steps;
uint reset_each;
unsigned long long iseed = 0ULL;
double t_phase_estimation;
double t_PE_factor;
int n_phase_estimation;
uint gCi;
uint c_acc = 0;
std::string Xmatstem="";

bool record_reverse=false;
std::vector<uint> reverse_counters;

pcg rangen;

ComplexVec gState;
////vector<double> energy_measures;
std::vector<double> X_measures;
std::vector<double> E_measures;


std::vector<Complex> rphase_m;

void fill_rphase(const uint& nlevels){
    rphase_m.resize(nlevels);
    uint c=1;
    for(uint i=0; i<nlevels; ++i){
        rphase_m[i].x = cos((2.0*M_PI/(double)c));
        rphase_m[i].y = sin((2.0*M_PI/(double)c));
        c<<=1;
	//printf("rphase_m[i] %.12lf %.12lf\n", real(rphase_m[i]), imag(rphase_m[i]));
    }
} 


// bitmap
std::vector<uint> bm_states;
std::vector<uint> bm_enes_old;
std::vector<uint> bm_enes_new;
uint bm_acc;


void fill_bitmap(){
    bm_states.resize(state_qbits);
    bm_enes_old.resize(ene_qbits);
    bm_enes_new.resize(ene_qbits);
    uint c=0;
    for(uint i=0; i< state_qbits; ++i)  bm_states[i] = c++;
    for(uint i=0; i< ene_qbits; ++i)    bm_enes_old[i] = c++;
    for(uint i=0; i< ene_qbits; ++i)    bm_enes_new[i] = c++;
    bm_acc = c;
}

// algorithm
uint W_mask;
std::vector<double> W_fs;
std::vector<std::vector<uint>> W_case_masks;

void fill_W_utils(double beta, double t_PE_factor){
    W_mask=0U;
    W_mask = (1U << bm_acc);
    for(uint i=0; i<ene_qbits; ++i)
        W_mask |= (1U << bm_enes_old[i]) | (1U << bm_enes_new[i]);

    // energy goes from 0 to (ene_levels-1)*t_PE_factor
    W_fs.resize(ene_levels);
    double c = beta/(t_PE_factor*ene_levels);
    for(uint i=0; i<ene_levels; ++i){
        W_fs[i] = exp(-(double)(i*c));
    }

    // mask cases
    W_case_masks = std::vector<std::vector<uint>>(ene_levels); 
    for(uint i=1; i<ene_levels; ++i){ // all possible (non-trivial) values of Delta E
        W_case_masks[i] = std::vector<uint>(ene_levels-i,(1U<<bm_acc));
        for(uint Ei=0; Ei<ene_levels-i; ++Ei){
            uint Ej=Ei+i;
            for(uint k=0; k<ene_qbits; ++k){
                W_case_masks[i][Ei] |= ((Ei>>k & 1U) << bm_enes_old[k]) | ((Ej>>k & 1U) << bm_enes_new[k]);
            }
        }
    }
}

uint creg_to_uint(const std::vector<uint>& c_reg){
    if(c_reg.size()<1)
        throw std::runtime_error("ERROR: size of register zero.");

    uint ret = c_reg[0];
    for(uint j=1U; j<c_reg.size(); ++j)
       ret += c_reg[j] << j; 

    return ret;
}
//XXX: the following two functions commented have parts define in suqa

void reset_non_state_qbits(ComplexVec& state){
//    DEBUG_CALL(cout<<"\n\nBefore reset"<<endl);
//    DEBUG_CALL(sparse_print(gState));
    std::vector<double> rgenerates(ene_qbits);

    for(auto& el : rgenerates) el = rangen.doub();
    suqa::apply_reset(state, bm_enes_old, rgenerates);

    for(auto& el : rgenerates) el = rangen.doub();
    suqa::apply_reset(state, bm_enes_new, rgenerates);

    suqa::apply_reset(state, bm_acc, rangen.doub());
//    DEBUG_CALL(cout<<"\n\nAfter reset"<<endl);
//    DEBUG_CALL(sparse_print(gState));
}


////TODO: can be optimized for multiple qbits measures?
void measure_qbits(ComplexVec& state, const std::vector<uint>& qs, std::vector<uint>& cs){
    for(uint k = 0U; k < qs.size(); ++k)
        suqa::measure_qbit(state, qs[k], cs[k], rangen.doub());
}


__global__ 
void kernel_qms_crm(Complex *const state, uint len, uint q_control, uint q_target, Complex rphase){
//    const Complex TWOSQINV_CMPX = make_cuDoubleComplex(TWOSQINV,0.0f);
     
    int i = blockDim.x*blockIdx.x + threadIdx.x;    
    while(i<len){
        if(((i >> q_control) & 1U) && ((i >> q_target) & 1U)){
            state[i]*= rphase;
        }
        i+=gridDim.x*blockDim.x;
    }
}


void qms_crm(ComplexVec& state, const uint& q_control, const uint& q_target, const int& m){
    Complex rphase = (m>0) ? rphase_m[m] : rphase_m[-m];
    if(m>0) rphase.y*=-1;

#if defined(CUDA_HOST)
    for(uint i = 0U; i < state.size(); ++i){
        // for the swap, not only q_target:1 but also q_control:1
        if(((i >> q_control) & 1U) && ((i >> q_target) & 1U)){
            state[i]*= rphase;
        }
    }
#else // CUDA defined
    kernel_qms_crm<<<suqa::blocks,suqa::threads>>>(state.data, state.size(), q_control, q_target, rphase);
#endif
    //TODO: implement kernel
}

void qms_qft(ComplexVec& state, const std::vector<uint>& qact){
    int qsize = qact.size();
    for(int outer_i=qsize-1; outer_i>=0; outer_i--){
        suqa::apply_h(state, qact[outer_i]);
        for(int inner_i=outer_i-1; inner_i>=0; inner_i--){
            qms_crm(state, qact[inner_i], qact[outer_i], -1-(outer_i-inner_i));
        }
    }
}


void qms_qft_inverse(ComplexVec& state, const std::vector<uint>& qact){
    int qsize = qact.size();
    for(int outer_i=0; outer_i<qsize; outer_i++){
        for(int inner_i=0; inner_i<outer_i; inner_i++){
            qms_crm(state, qact[inner_i], qact[outer_i], 1+(outer_i-inner_i));
        }
        suqa::apply_h(state, qact[outer_i]);
    }
}

void apply_phase_estimation(ComplexVec& state, const std::vector<uint>& q_state, const std::vector<uint>& q_target, const double& t, const uint& n){
//    DEBUG_CALL(cout<<"apply_phase_estimation()"<<endl);
    suqa::apply_h(state,q_target);
//    DEBUG_CALL(cout<<"after qi_h(state,q_target)"<<endl);
//    DEBUG_CALL(sparse_print(state));

    // apply CUs
    for(int trg = q_target.size() - 1; trg > -1; --trg){
        uint powr = (1U << (q_target.size()-1-trg));
        cevolution(state, powr*t, powr*n, q_target[trg], q_state);
    }
//    DEBUG_CALL(cout<<"\nafter evolutions"<<endl);
//    DEBUG_CALL(sparse_print(state));
    
    // apply QFT^{-1}
    qms_qft_inverse(state, q_target); 

}

void apply_phase_estimation_inverse(ComplexVec& state, const std::vector<uint>& q_state, const std::vector<uint>& q_target, const double& t, const uint& n){
//    DEBUG_CALL(cout<<"apply_phase_estimation_inverse()"<<endl);

    // apply QFT
    qms_qft(state, q_target); 


    // apply CUs
    for(uint trg = 0; trg < q_target.size(); ++trg){
        uint powr = (1U << (q_target.size()-1-trg));
        cevolution(state, -powr*t, powr*n, q_target[trg], q_state);
    }
    
    suqa::apply_h(state,q_target);

}


void apply_Phi_old(){

    apply_phase_estimation(gState, bm_states, bm_enes_old, t_phase_estimation, n_phase_estimation);

}

void apply_Phi_old_inverse(){

    apply_phase_estimation_inverse(gState, bm_states, bm_enes_old, t_phase_estimation, n_phase_estimation);

}

void apply_Phi(){

    apply_phase_estimation(gState, bm_states, bm_enes_new, t_phase_estimation, n_phase_estimation);

}

void apply_Phi_inverse(){

    apply_phase_estimation_inverse(gState, bm_states, bm_enes_new, t_phase_estimation, n_phase_estimation);

}


uint draw_C(){
    std::vector<double> C_weigthsums = get_C_weigthsums();
    double extract = rangen.doub();
    for(uint Ci =0U; Ci < C_weigthsums.size(); ++Ci){
        if(extract<C_weigthsums[Ci]){
            return Ci;
        }
    }
    return C_weigthsums.size();
    return 0; //FIXME: to remove
}


void apply_W(){
//    DEBUG_CALL(cout<<"\n\nApply W"<<endl);
//
    
//    for(uint i = 0U; i < gState.size(); ++i){
//        bool matching = false;
//        uint dE;
//        for(dE=1; dE<ene_levels; ++dE){
//            for(uint k=0; k<W_case_masks[dE].size() && !matching; ++k){
//                matching = ((i & W_mask) == W_case_masks[dE][k]);
//            }
//            if(matching)
//                break;
//        }
//        if(matching){
//            uint j = i & ~(1U << bm_acc);
//            const double fdE = W_fs[dE];
//            DEBUG_CALL(if(norm(gState[i])+norm(gState[j])>1e-8) cout<<"case1: gState["<<i<<"] = "<<gState[i]<<", gState["<<j<<"] = "<<gState[j]<<endl);
//            suqa::apply_2x2mat<Complex>(gState[j], gState[i], sqrt(1.-fdE), sqrt(fdE), sqrt(fdE), -sqrt(1.-fdE));
//            DEBUG_CALL(if(norm(gState[i])+norm(gState[j])>1e-8) cout<<"after: gState["<<i<<"] = "<<gState[i]<<", gState["<<j<<"] = "<<gState[j]<<endl);
//        }else if((i >> bm_acc) & 1U){
//            uint j = i & ~(1U << bm_acc);
//
//            DEBUG_CALL(if(norm(gState[i])+norm(gState[j])>1e-8) cout<<"case3: gState["<<i<<"] = "<<gState[i]<<", gState["<<j<<"] = "<<gState[j]<<endl);
//            std::swap(gState[i],gState[j]);
//            DEBUG_CALL(if(norm(gState[i])+norm(gState[j])>1e-8) cout<<"after: gState["<<i<<"] = "<<gState[i]<<", gState["<<j<<"] = "<<gState[j]<<endl);
//        }
//    }
}

void apply_W_inverse(){
    apply_W();
}

void apply_U(){
//    DEBUG_CALL(cout<<"\n\nApply U"<<endl);
    apply_C(gState, bm_states, gCi);
//    DEBUG_CALL(cout<<"\n\nAfter apply C = "<<gCi<<endl);
//    DEBUG_CALL(sparse_print(gState));
//
//
//
//
    apply_Phi();
//    DEBUG_CALL(cout<<"\n\nAfter second phase estimation"<<endl);
//    DEBUG_CALL(sparse_print(gState));
//
//
//
    apply_W();
//    DEBUG_CALL(cout<<"\n\nAfter apply W"<<endl);
//    DEBUG_CALL(sparse_print(gState));
}

void apply_U_inverse(){
//    apply_W_inverse();
//    DEBUG_CALL(cout<<"\n\nAfter apply W inverse"<<endl);
//    DEBUG_CALL(sparse_print(gState));
//    apply_Phi_inverse();
//    DEBUG_CALL(cout<<"\n\nAfter inverse second phase estimation"<<endl);
//    DEBUG_CALL(sparse_print(gState));
//    apply_C_inverse(gState,bm_states,gCi);
//    DEBUG_CALL(cout<<"\n\nAfter apply C inverse = "<<gCi<<endl);
//    DEBUG_CALL(sparse_print(gState));
}


void init_measure_structs(){
    
    fill_meas_cache(bm_states, Xmatstem);
}

double measure_X(){
//    if(Xmatstem==""){
//        return 0.0;
//    }
//
//	vector<uint> classics(state_qbits);
//    
//    apply_measure_rotation(gState);
//
//    measure_qbits(gState, bm_states, classics);
//
//    apply_measure_antirotation(gState);
//
    uint meas = 0U;
//    for(uint i=0; i<state_qbits; ++i){
//        meas |= (classics[i] << i);
//    }
//
    return get_meas_opvals(meas);
}

int metro_step(bool take_measure){
    // return values:
    // 1 -> step accepted, not measured
    // 2 -> step accepted, measured
    // 3 -> step rejected and restored, not measured
    // 4 -> step rejected and restored, measured
    // -1 -> step rejected non restored 
    int ret=0;
    
//    DEBUG_CALL(cout<<"initial state"<<endl);
//    DEBUG_CALL(sparse_print(gState));
    reset_non_state_qbits(gState);
//    DEBUG_CALL(cout<<"state after reset"<<endl);
//    DEBUG_CALL(sparse_print(gState));
    apply_Phi_old();
//    DEBUG_CALL(cout<<"\n\nAfter first phase estimation"<<endl);
//    DEBUG_CALL(sparse_print(gState));


    gCi = draw_C();
//    DEBUG_CALL(cout<<"\n\ndrawn C = "<<gCi<<endl);
    apply_U();
//
//
//    measure_qbit(gState, bm_acc, c_acc);
//
//    if (c_acc == 1U){
//        DEBUG_CALL(cout<<"accepted"<<endl);
//        double Enew_meas_d;
//        vector<uint> c_E_news(ene_qbits,0), c_E_olds(ene_qbits,0);
//        measure_qbits(gState, bm_enes_new, c_E_news);
//        DEBUG_CALL(double tmp_E=creg_to_uint(c_E_news)/(double)(t_PE_factor*ene_levels));
//        DEBUG_CALL(cout<<"  energy measure : "<<tmp_E<<endl); 
//        apply_Phi_inverse();
//        if(take_measure){
//            Enew_meas_d = creg_to_uint(c_E_news)/(double)(t_PE_factor*ene_levels);
//            E_measures.push_back(Enew_meas_d);
//            suqa::qi_reset(gState, bm_enes_new);
//            X_measures.push_back(measure_X());
//////            X_measures.push_back(0.0);
//            DEBUG_CALL(cout<<"  X measure : "<<X_measures.back()<<endl); 
//            DEBUG_CALL(cout<<"\n\nAfter X measure"<<endl);
//            DEBUG_CALL(sparse_print(gState));
//            DEBUG_CALL(cout<<"  X measure : "<<X_measures.back()<<endl); 
////            reset_non_state_qbits();
//            suqa::qi_reset(gState, bm_enes_new);
//            apply_Phi();
//            measure_qbits(gState, bm_enes_new, c_E_news);
//            DEBUG_CALL(cout<<"\n\nAfter E recollapse"<<endl);
//            DEBUG_CALL(sparse_print(gState));
//            apply_Phi_inverse();
//
//            ret = 2; // step accepted, measured
//        }else{
//            ret = 1; // step accepted, not measured
//        }
//        return ret;
//    }
//    //else
//
//    DEBUG_CALL(cout<<"rejected; restoration cycle:"<<endl);
//    apply_U_inverse();
//
//    DEBUG_CALL(cout<<"\n\nBefore reverse attempts"<<endl);
//    DEBUG_CALL(sparse_print(gState));
//    uint iters = 0;
//    while(iters < max_reverse_attempts){
//        apply_Phi();
//        uint Eold_meas, Enew_meas;
//        double Eold_meas_d;
//        vector<uint> c_E_olds(ene_qbits,0), c_E_news(ene_qbits,0);
//        measure_qbits(gState, bm_enes_old, c_E_olds);
//        Eold_meas = creg_to_uint(c_E_olds);
//        Eold_meas_d = Eold_meas/(double)(t_PE_factor*ene_levels);
//        measure_qbits(gState, bm_enes_new, c_E_news);
//        Enew_meas = creg_to_uint(c_E_news);
//        apply_Phi_inverse();
//        
//        if(Eold_meas == Enew_meas){
//            DEBUG_CALL(cout<<"  accepted restoration ("<<max_reverse_attempts-iters<<"/"<<max_reverse_attempts<<")"<<endl); 
//            if(take_measure){
//                E_measures.push_back(Eold_meas_d);
//                DEBUG_CALL(cout<<"  energy measure : "<<Eold_meas_d<<endl); 
//                DEBUG_CALL(cout<<"\n\nBefore X measure"<<endl);
//                DEBUG_CALL(sparse_print(gState));
//                suqa::qi_reset(gState, bm_enes_new);
//                X_measures.push_back(measure_X());
//                DEBUG_CALL(cout<<"\n\nAfter X measure"<<endl);
//                DEBUG_CALL(sparse_print(gState));
//                DEBUG_CALL(cout<<"  X measure : "<<X_measures.back()<<endl); 
//                suqa::qi_reset(gState, bm_enes_new);
//                apply_Phi();
//                measure_qbits(gState, bm_enes_new, c_E_news);
//                DEBUG_CALL(cout<<"\n\nAfter E recollapse"<<endl);
//                DEBUG_CALL(sparse_print(gState));
//                apply_Phi_inverse();
//
//                ret=4;
//            }else{
//                ret=3;
//            }
//            break;
//        }
//        //else
//        DEBUG_CALL(cout<<"  rejected ("<<max_reverse_attempts-iters<<"/"<<max_reverse_attempts<<")"<<endl); 
//        uint c_acc_trash;
//        apply_U(); 
//        measure_qbit(gState, bm_acc, c_acc_trash); 
//        apply_U_inverse(); 
//
//        iters++;
//    }
//
//    if(record_reverse){
//        reverse_counters.push_back(iters);
//    }
//
//    if (iters == max_reverse_attempts){
//        DEBUG_CALL(cout<<("not converged in "+to_string(max_reverse_attempts)+" steps :(")<<endl);
//
//        ret = -1;
//        return ret;
//    }else{
//        return ret;
//    }
//
    return ret;
}


}