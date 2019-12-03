#pragma once
#include "include/Rand.hpp"
#include "include/io.hpp"
#include "include/suqa_gates.hpp"

void cevolution(std::vector<std::complex<double>>& state, const double& t, const int& n, const uint& q_control, const std::vector<uint>& qstate);

namespace qms{

uint state_qbits;
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
string Xmatstem="";

bool record_reverse=false;
vector<uint> reverse_counters;


vector<Complex> gState;
//vector<double> energy_measures;
vector<double> X_measures;
vector<double> E_measures;


vector<Complex> rphase_m;

void fill_rphase(const uint& nlevels){
    rphase_m.resize(nlevels);
    uint c=1;
    for(uint i=0; i<nlevels; ++i){
        rphase_m[i] = exp((2.0*M_PI/(double)c)*iu);
        c<<=1;
	//printf("rphase_m[i] %.12lf %.12lf\n", real(rphase_m[i]), imag(rphase_m[i]));
    }
} 

pcg rangen;

// bitmap
vector<uint> bm_states;
vector<uint> bm_enes_old;
vector<uint> bm_enes_new;
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
vector<double> W_fs;
vector<vector<uint>> W_case_masks;

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
    W_case_masks = vector<vector<uint>>(ene_levels); 
    for(uint i=1; i<ene_levels; ++i){ // all possible (non-trivial) values of Delta E
        W_case_masks[i] = vector<uint>(ene_levels-i,(1U<<bm_acc));
        for(uint Ei=0; Ei<ene_levels-i; ++Ei){
            uint Ej=Ei+i;
            for(uint k=0; k<ene_qbits; ++k){
                W_case_masks[i][Ei] |= ((Ei>>k & 1U) << bm_enes_old[k]) | ((Ej>>k & 1U) << bm_enes_new[k]);
            }
        }
    }
}

uint creg_to_uint(const vector<uint>& c_reg){
    if(c_reg.size()<1)
        throw std::runtime_error("ERROR: size of register zero.");

    uint ret = c_reg[0];
    for(uint j=1U; j<c_reg.size(); ++j)
       ret += c_reg[j] << j; 

    return ret;
}

void reset_non_state_qbits(vector<Complex>& state){
    DEBUG_CALL(cout<<"\n\nBefore reset"<<endl);
    DEBUG_CALL(sparse_print(gState));
    qi_reset(state, bm_enes_old);
    qi_reset(state, bm_enes_new);
    qi_reset(state, bm_acc);
    DEBUG_CALL(cout<<"\n\nAfter reset"<<endl);
    DEBUG_CALL(sparse_print(gState));
}

void measure_qbit(vector<Complex>& state, const uint& q, uint& c){
    double prob1 = 0.0;

    for(uint i = 0U; i < state.size(); ++i){
        if((i >> q) & 1U){
            prob1+=norm(state[i]); 
        }
    }
    double rdoub = rangen.doub();
    DEBUG_CALL(cout<<"prob1 = "<<prob1<<", rdoub = "<<rdoub<<endl);
    c = (uint)(rdoub < prob1); // prob1=1 -> c = 1 surely
    
    if(c){ // set to 0 coeffs with bm_acc 0
        for(uint i = 0U; i < state.size(); ++i){
            if(((i >> q) & 1U) == 0U)
                state[i] = {0.0, 0.0};        
        }
    }else{ // set to 0 coeffs with bm_acc 1
        for(uint i = 0U; i < state.size(); ++i){
            if(((i >> q) & 1U) == 1U)
                state[i] = {0.0, 0.0};        
        }
    }
    vnormalize(state);
}

//TODO: can be optimized for multiple qbits measures?
void measure_qbits(vector<Complex>& state, const vector<uint>& qs, vector<uint>& cs){
    for(uint k = 0U; k < qs.size(); ++k)
        measure_qbit(state, qs[k], cs[k]);
}


void qi_crm(vector<Complex>& state, const uint& q_control, const uint& q_target, const int& m){
    for(uint i = 0U; i < state.size(); ++i){
        // for the swap, not only q_target:1 but also q_control:1
        if(((i >> q_control) & 1U) && ((i >> q_target) & 1U)){
            state[i] *= (m>0) ? rphase_m[m] : conj(rphase_m[-m]);
        }
    }
}

void qi_qft(vector<Complex>& state, const vector<uint>& qact){
    int qsize = qact.size();
    for(int outer_i=qsize-1; outer_i>=0; outer_i--){
        qi_h(state, qact[outer_i]);
        for(int inner_i=outer_i-1; inner_i>=0; inner_i--){
            qi_crm(state, qact[inner_i], qact[outer_i], -1-(outer_i-inner_i));
        }
    }
}


void qi_qft_inverse(vector<Complex>& state, const vector<uint>& qact){
    int qsize = qact.size();
    for(int outer_i=0; outer_i<qsize; outer_i++){
        for(int inner_i=0; inner_i<outer_i; inner_i++){
            qi_crm(state, qact[inner_i], qact[outer_i], 1+(outer_i-inner_i));
        }
        qi_h(state, qact[outer_i]);
    }
}

void apply_phase_estimation(vector<Complex>& state, const vector<uint>& q_state, const vector<uint>& q_target, const double& t, const uint& n){
    DEBUG_CALL(cout<<"apply_phase_estimation()"<<endl);
    qi_h(state,q_target);
    DEBUG_CALL(cout<<"after qi_h(state,q_target)"<<endl);
    DEBUG_CALL(sparse_print(state));

    // apply CUs
    for(int trg = q_target.size() - 1; trg > -1; --trg){
        for(uint itrs = 0; itrs < q_target.size()-trg; ++itrs){
            cevolution(state, t, n, q_target[trg], q_state);
        }
    }
    DEBUG_CALL(cout<<"\nafter evolutions"<<endl);
    DEBUG_CALL(sparse_print(state));
    
    // apply QFT^{-1}
    qi_qft_inverse(state, q_target); 

}

void apply_phase_estimation_inverse(vector<Complex>& state, const vector<uint>& q_state, const vector<uint>& q_target, const double& t, const uint& n){
    DEBUG_CALL(cout<<"apply_phase_estimation_inverse()"<<endl);

    // apply QFT
    qi_qft(state, q_target); 


    // apply CUs
    for(uint trg = 0; trg < q_target.size(); ++trg){
        for(uint itrs = 0; itrs < q_target.size()-trg; ++itrs){
            for(uint ti = 0; ti < n; ++ti){
                cevolution(state, -t, n, q_target[trg], q_state);
            }
        }
    }
    
    qi_h(state,q_target);

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
    double extract = rangen.doub();
    if (extract<1./3)
        return 0U;
    else if(extract<2./3)
        return 1U;
    else
        return 2U;
}

void apply_C(const uint &Ci){
    if(Ci==0U){
        qi_h(gState,bm_states[0]);
    }else if(Ci==1U){
        qi_h(gState,bm_states[1]);
    }else if(Ci==2U){
        qi_h(gState,bm_states[2]);
    }else{
        throw std::runtime_error("Error!");
    }
}

void apply_C_inverse(const uint &Ci){
    apply_C(Ci);
}

void apply_W(){
    DEBUG_CALL(cout<<"\n\nApply W"<<endl);

    
    for(uint i = 0U; i < gState.size(); ++i){
        bool matching = false;
        uint dE;
        for(dE=1; dE<ene_levels; ++dE){
            for(uint k=0; k<W_case_masks[dE].size() && !matching; ++k){
                matching = ((i & W_mask) == W_case_masks[dE][k]);
            }
            if(matching)
                break;
        }
        if(matching){
            uint j = i & ~(1U << bm_acc);
            const double fdE = W_fs[dE];
            DEBUG_CALL(if(norm(gState[i])+norm(gState[j])>1e-8) cout<<"case1: gState["<<i<<"] = "<<gState[i]<<", gState["<<j<<"] = "<<gState[j]<<endl);
            apply_2x2mat(gState[j], gState[i], sqrt(1.-fdE), sqrt(fdE), sqrt(fdE), -sqrt(1.-fdE));
            DEBUG_CALL(if(norm(gState[i])+norm(gState[j])>1e-8) cout<<"after: gState["<<i<<"] = "<<gState[i]<<", gState["<<j<<"] = "<<gState[j]<<endl);
        }else if((i >> bm_acc) & 1U){
            uint j = i & ~(1U << bm_acc);

            DEBUG_CALL(if(norm(gState[i])+norm(gState[j])>1e-8) cout<<"case3: gState["<<i<<"] = "<<gState[i]<<", gState["<<j<<"] = "<<gState[j]<<endl);
            std::swap(gState[i],gState[j]);
            DEBUG_CALL(if(norm(gState[i])+norm(gState[j])>1e-8) cout<<"after: gState["<<i<<"] = "<<gState[i]<<", gState["<<j<<"] = "<<gState[j]<<endl);
        }
    }
}

void apply_W_inverse(){
    apply_W();
}

void apply_U(){
    DEBUG_CALL(cout<<"\n\nApply U"<<endl);
    apply_C(gCi);
    DEBUG_CALL(cout<<"\n\nAfter apply C = "<<gCi<<endl);
    DEBUG_CALL(sparse_print(gState));




    apply_Phi();
    DEBUG_CALL(cout<<"\n\nAfter second phase estimation"<<endl);
    DEBUG_CALL(sparse_print(gState));



    apply_W();
    DEBUG_CALL(cout<<"\n\nAfter apply W"<<endl);
    DEBUG_CALL(sparse_print(gState));
}

void apply_U_inverse(){
    apply_W_inverse();
    DEBUG_CALL(cout<<"\n\nAfter apply W inverse"<<endl);
    DEBUG_CALL(sparse_print(gState));
    apply_Phi_inverse();
    DEBUG_CALL(cout<<"\n\nAfter inverse second phase estimation"<<endl);
    DEBUG_CALL(sparse_print(gState));
    apply_C_inverse(gCi);
    DEBUG_CALL(cout<<"\n\nAfter apply C inverse = "<<gCi<<endl);
    DEBUG_CALL(sparse_print(gState));
}


Complex SXmat[8][8];

double measure_X(){
    if(Xmatstem==""){
        return 0.0;
    }

	uint mask = 7U;
	vector<uint> classics(3);

    vector<double> vals(8);

    FILE * fil_re = fopen((Xmatstem+"_vecs_re").c_str(),"r"); 
    FILE * fil_im = fopen((Xmatstem+"_vecs_im").c_str(),"r"); 
    FILE * fil_vals = fopen((Xmatstem+"_vals").c_str(),"r"); 
    double tmp_re,tmp_im;
    for(int i=0; i<8; ++i){
        fscanf(fil_vals, "%lg",&vals[i]);
//        cout<<"vals = "<<vals[i]<<endl;
        for(int j=0; j<8; ++j){
            fscanf(fil_re, "%lg",&tmp_re);
            fscanf(fil_im, "%lg",&tmp_im);
            SXmat[i][j] = tmp_re+tmp_im*iu;
//            cout<<real(SXmat[i][j])<<imag(SXmat[i][j])<<" ";
        }
        fscanf(fil_re, "\n");
        fscanf(fil_im, "\n");
//        cout<<endl;
    }

    fclose(fil_vals);
    fclose(fil_re);
    fclose(fil_im);

    vector<uint> iss(8);
    vector<Complex> ass(8);
    
	for(uint i_0 = 0U; i_0 < gState.size(); ++i_0){
        if((i_0 & mask) == 0U){
      
            iss[0] = i_0;
            iss[1] = i_0 | 1U;
            iss[2] = i_0 | 2U;
            iss[3] = i_0 | 3U;
            iss[4] = i_0 | 4U;
            iss[5] = i_0 | 5U;
            iss[6] = i_0 | 6U;
            iss[7] = i_0 | 7U;


            ass[0] = gState[iss[0]];
            ass[1] = gState[iss[1]];
            ass[2] = gState[iss[2]];
            ass[3] = gState[iss[3]];
            ass[4] = gState[iss[4]];
            ass[5] = gState[iss[5]];
            ass[6] = gState[iss[6]];
            ass[7] = gState[iss[7]];

            for(int r=0; r<8; ++r){
                gState[iss[r]]=0.0;
                for(int c=0; c<8; ++c){
                     gState[iss[r]] += SXmat[r][c]*ass[c];
                }
            }
            

//            gState[i_0] = (cos(dtp)*a_0 -sin(dtp)*iu*a_6);
//            gState[i_1] = (cos(dtp)*a_1 -sin(dtp)*iu*a_7);
//            gState[i_2] = (cos(dtp)*a_2 -sin(dtp)*iu*a_4);
//            gState[i_3] = (cos(dtp)*a_3 -sin(dtp)*iu*a_5);
//            gState[i_4] = (cos(dtp)*a_4 -sin(dtp)*iu*a_2);
//            gState[i_5] = (cos(dtp)*a_5 -sin(dtp)*iu*a_3);
//            gState[i_6] = (cos(dtp)*a_6 -sin(dtp)*iu*a_0);
//            gState[i_7] = (cos(dtp)*a_7 -sin(dtp)*iu*a_1);

        }
    }
    measure_qbits(gState, bm_states, classics);

	for(uint i_0 = 0U; i_0 < gState.size(); ++i_0){
        if((i_0 & mask) == 0U){
            iss[0] = i_0;
            iss[1] = i_0 | 1U;
            iss[2] = i_0 | 2U;
            iss[3] = i_0 | 3U;
            iss[4] = i_0 | 4U;
            iss[5] = i_0 | 5U;
            iss[6] = i_0 | 6U;
            iss[7] = i_0 | 7U;


            ass[0] = gState[iss[0]];
            ass[1] = gState[iss[1]];
            ass[2] = gState[iss[2]];
            ass[3] = gState[iss[3]];
            ass[4] = gState[iss[4]];
            ass[5] = gState[iss[5]];
            ass[6] = gState[iss[6]];
            ass[7] = gState[iss[7]];

            for(int r=0; r<8; ++r){
                gState[iss[r]]=0.0;
                for(int c=0; c<8; ++c){
                     gState[iss[r]] += conj(SXmat[c][r])*ass[c];
                }
            }
        }
    }

    uint meas = classics[0] + 2*classics[1] + 4*classics[2];
    return vals[meas];
//    switch(meas){
//        case 0:
//            return vals[0];
//            break;
//        case 1:
//            return phi;
//            break;
//        case 2:
//            return mphi_inv;
//            break;
//        default:
//            throw "Error!";
//    }
//    return 0.0;
}

// double measure_X(){
// 	vector<uint> classics(2);
//     measure_qbits(gState, {bm_psi0,bm_psi1}, classics);
//     uint meas = classics[0] + 2*classics[1];
//     switch(meas){
//         case 0:
//             return 1.0;
//             break;
//         case 1:
//             return 2.0;
//             break;
//         case 2:
//             return 3.0;
//             break;
//         default:
//             throw "Error!";
//     }
//     return 0.0;
// }


int metro_step(bool take_measure){
    // return values:
    // 1 -> step accepted, not measured
    // 2 -> step accepted, measured
    // 3 -> step rejected and restored, not measured
    // 4 -> step rejected and restored, measured
    // -1 -> step rejected non restored 
    int ret=0;
    
    DEBUG_CALL(cout<<"initial state"<<endl);
    DEBUG_CALL(sparse_print(gState));
    reset_non_state_qbits(gState);
    DEBUG_CALL(cout<<"state after reset"<<endl);
    DEBUG_CALL(sparse_print(gState));
    apply_Phi_old();
    DEBUG_CALL(cout<<"\n\nAfter first phase estimation"<<endl);
    DEBUG_CALL(sparse_print(gState));


    gCi = draw_C();
    DEBUG_CALL(cout<<"\n\ndrawn C = "<<gCi<<endl);
    apply_U();


    measure_qbit(gState, bm_acc, c_acc);

    if (c_acc == 1U){
        DEBUG_CALL(cout<<"accepted"<<endl);
        double Enew_meas_d;
        vector<uint> c_E_news(ene_qbits,0), c_E_olds(ene_qbits,0);
        measure_qbits(gState, bm_enes_new, c_E_news);
        DEBUG_CALL(double tmp_E=creg_to_uint(c_E_news)/(double)(t_PE_factor*ene_levels));
        DEBUG_CALL(cout<<"  energy measure : "<<tmp_E<<endl); 
        apply_Phi_inverse();
        if(take_measure){
            Enew_meas_d = creg_to_uint(c_E_news)/(double)(t_PE_factor*ene_levels);
            E_measures.push_back(Enew_meas_d);
            qi_reset(gState, bm_enes_new);
            X_measures.push_back(measure_X());
////            X_measures.push_back(0.0);
            DEBUG_CALL(cout<<"  X measure : "<<X_measures.back()<<endl); 
            DEBUG_CALL(cout<<"\n\nAfter X measure"<<endl);
            DEBUG_CALL(sparse_print(gState));
            DEBUG_CALL(cout<<"  X measure : "<<X_measures.back()<<endl); 
//            reset_non_state_qbits();
            qi_reset(gState, bm_enes_new);
            apply_Phi();
            measure_qbits(gState, bm_enes_new, c_E_news);
            DEBUG_CALL(cout<<"\n\nAfter E recollapse"<<endl);
            DEBUG_CALL(sparse_print(gState));
            apply_Phi_inverse();

            ret = 2; // step accepted, measured
        }else{
            ret = 1; // step accepted, not measured
        }
        return ret;
    }
    //else

    DEBUG_CALL(cout<<"rejected; restoration cycle:"<<endl);
    apply_U_inverse();

    DEBUG_CALL(cout<<"\n\nBefore reverse attempts"<<endl);
    DEBUG_CALL(sparse_print(gState));
    uint iters = 0;
    while(iters < max_reverse_attempts){
        apply_Phi();
        uint Eold_meas, Enew_meas;
        double Eold_meas_d;
        vector<uint> c_E_olds(ene_qbits,0), c_E_news(ene_qbits,0);
        measure_qbits(gState, bm_enes_old, c_E_olds);
        Eold_meas = creg_to_uint(c_E_olds);
        Eold_meas_d = Eold_meas/(double)(t_PE_factor*ene_levels);
        measure_qbits(gState, bm_enes_new, c_E_news);
        Enew_meas = creg_to_uint(c_E_news);
        apply_Phi_inverse();
        
        if(Eold_meas == Enew_meas){
            DEBUG_CALL(cout<<"  accepted restoration ("<<max_reverse_attempts-iters<<"/"<<max_reverse_attempts<<")"<<endl); 
            if(take_measure){
                E_measures.push_back(Eold_meas_d);
                DEBUG_CALL(cout<<"  energy measure : "<<Eold_meas_d<<endl); 
                DEBUG_CALL(cout<<"\n\nBefore X measure"<<endl);
                DEBUG_CALL(sparse_print(gState));
                qi_reset(gState, bm_enes_new);
                X_measures.push_back(measure_X());
                DEBUG_CALL(cout<<"\n\nAfter X measure"<<endl);
                DEBUG_CALL(sparse_print(gState));
                DEBUG_CALL(cout<<"  X measure : "<<X_measures.back()<<endl); 
                qi_reset(gState, bm_enes_new);
                apply_Phi();
                measure_qbits(gState, bm_enes_new, c_E_news);
                DEBUG_CALL(cout<<"\n\nAfter E recollapse"<<endl);
                DEBUG_CALL(sparse_print(gState));
                apply_Phi_inverse();

                ret=4;
            }else{
                ret=3;
            }
            break;
        }
        //else
        DEBUG_CALL(cout<<"  rejected ("<<max_reverse_attempts-iters<<"/"<<max_reverse_attempts<<")"<<endl); 
        uint c_acc_trash;
        apply_U(); 
        measure_qbit(gState, bm_acc, c_acc_trash); 
        apply_U_inverse(); 

        iters++;
    }

    if(record_reverse){
        reverse_counters.push_back(iters);
    }

    if (iters == max_reverse_attempts){
        DEBUG_CALL(cout<<("not converged in "+to_string(max_reverse_attempts)+" steps :(")<<endl);

        ret = -1;
        return ret;
    }else{
        return ret;
    }

    return 0;
}


}