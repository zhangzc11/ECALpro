#!/bin/bash                                                                        
                                                                                  
iter_ini=1
iter_fin=1  # it is included in sequence below                                   

eosPrefix="root://eoscms//eos/cms"                                        
wwwPath="/afs/cern.ch/user/m/mciprian/www/pi0calib/ICplot_Legacy/"                             
eosPath="/store/group/dpg_ecal/alca_ecalcalib/piZero_Run2/mciprian/"
#dirName="AlcaP0_Run2016G_sel16_reg12"
dirName="AlCaP0_AllRun2017_condor"
tagName="${dirName}_"
draw_Rooplot0_canvas1=0

useMergedFitFile=false # when true, no need to specify a file index
BarrelOrEndcap="Barrel"  # Barrel, Endcap
#fileIndex=14 # index for EB goes from 0 to 30 and for EE it goes from 0 to 7
fileIndexIni=7
fileIndexFin=7
#BarrelOrEndcap="Endcap"  # Barrel, Endcap

nFitsToPlot=10  # there are at most 2000 plots in each file

# if fitIndexToPlot>=0, look for this specific index in the rooplots in the file and just plot that one
# if negative just plot nFistToPlot plots
fitIndexToPlot="15784"  
#fitIndexToPlot="-1"  

for i in `seq $iter_ini $iter_fin`
do

    for fileIndex in `seq $fileIndexIni $fileIndexFin`
    do


	outputDIR=${wwwPath}${dirName}"/iter_${i}/fitResPlots/${BarrelOrEndcap}/"
	echo "iter_${i}"
	if [ "$useMergedFitFile" = true ]; then
	    file="${eosPrefix}${eosPath}${dirName}/iter_${i}/${tagName}All${BarrelOrEndcap}_fitRes.root" 
	else
	    file="${eosPrefix}${eosPath}${dirName}/iter_${i}/${tagName}${BarrelOrEndcap}_${fileIndex}_fitRes.root" 
	fi
	echo "file --> ${file}"
	echo "output directory --> ${outputDIR}"
	root -l -b -q 'drawFitsSingleFile.C+("'${file}'","'${BarrelOrEndcap}'","'${outputDIR}'",'${nFitsToPlot}','${fitIndexToPlot}','${draw_Rooplot0_canvas1}')'

    done

done

