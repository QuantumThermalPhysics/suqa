import matplotlib.pyplot as plt
import sys
import numpy as np

if len(sys.argv)<2:
    print("needs file stem")
    sys.exit(1)

fig = plt.figure(figsize=(8, 6))
ax1 = fig.add_subplot(211)
ax2 = fig.add_subplot(212)

ax1.set_xlabel(r"$\beta$")
ax2.set_xlabel(r"$\beta$")
ax1.set_xlabel(r"Averages")
ax2.set_xlabel(r"Discrepancy")

bs=np.array([0.1,0.2,0.3,0.4,0.5,0.6,0.7,0.8,0.9,1.0])
kappas = np.array([1,2,4,8,16,20,40,60,80,100,150,200,250,300,400])

aa=[]
for vl in bs:
    print("beta = ", vl)
    aa.append(np.loadtxt(sys.argv[1]+"_b_"+str(vl)))
ndata = aa[0].shape[0]
vs=[np.mean(ael[:,1]) for ael in aa]
ss=[]
for i in range(len(kappas)):
    if(ndata/kappas[i] < 50):
        break
    thermpart = int(ndata-int(ndata/kappas[i])*kappas[i])
    print("cut: ",thermpart, ", blocksize: ",kappas[i], ", numblocks: ",int(ndata/kappas[i]))
    ss.append([np.std(np.mean(ael[thermpart:,1].reshape((int(ndata/kappas[i]),kappas[i])), axis = 1))/np.sqrt(ndata/kappas[i]) for ael in aa])

Hs=np.array([1.,0.,2.])
def Z(b):
    return np.sum(np.exp(-np.outer(b, Hs)),axis=1)
ene_exact=np.sum(np.exp(-np.outer(bs, Hs))*Hs,axis=1)/Z(bs)
ax1.plot(bs,ene_exact, label=r'$\langle E \rangle(\beta)$ (exact)')
ax1.errorbar(bs,vs,ss[0],linestyle="",capsize=3, label=r'$\langle E \rangle(\beta)$  (data)', ecolor="r")
for sss in ss[1:]:
    ax1.errorbar(bs,vs,sss,linestyle="",capsize=3, ecolor="r")

ax2.plot(bs,0.0*bs, label=r'Baseline')
ax2.errorbar(bs,vs-ene_exact,ss[0],linestyle="",capsize=3, label=r'$\langle E \rangle(\beta) - \langle E \rangle_{exact}$  (data)', ecolor="r")
for sss in ss[1:]:
    ax2.errorbar(bs,vs-ene_exact,sss,linestyle="",capsize=3, ecolor="r")

if aa[0].shape[1]>2:
    vsX=[np.mean(ael[:,2]) for ael in aa]
    ssX=[]
    for i in range(len(kappas)):
        if(ndata/kappas[i] < 50):
            break
        thermpart = int(ndata-int(ndata/kappas[i])*kappas[i])
        ssX.append([np.std(np.mean(ael[thermpart:,2].reshape((int(ndata/kappas[i]),kappas[i])), axis = 1))/np.sqrt(ndata/kappas[i]) for ael in aa])
    X_exact = np.exp(-bs)/Z(bs)
    ax1.plot(bs,X_exact, label=r'$\langle X \rangle(\beta)$ (exact)')
#    plot(bs,(np.exp(-bs)+5./2.*np.exp(-bs*0.0)+5./2.*np.exp(-2*bs))/Z(bs), label=r'X operator')
    ax1.errorbar(bs,vsX,ssX[0],linestyle="",capsize=3, label=r'$\langle X \rangle(\beta)$ (data)', ecolor='g')
    for sss in ssX[1:]:
        ax1.errorbar(bs,vsX,sss,linestyle="",capsize=3, ecolor="g")

    ax2.errorbar(bs,vsX-X_exact, ssX[0],linestyle="",capsize=3, label=r'$\langle X \rangle_{data} - \langle X \rangle_{exact}$', ecolor='g')
    for sss in ssX[1:]:
        ax2.errorbar(bs,vsX-X_exact,sss,linestyle="",capsize=3, ecolor="g")
ax1.legend()
ax2.legend()
plt.tight_layout()
fig.savefig(sys.argv[1]+"_analysis.pdf",dpi=200)
plt.show()