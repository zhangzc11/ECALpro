#! /bin/bash

dayMonthYear=`date +%d_%m_%Y`
dataset="AlCaP0"
runYear="2017"
#JsonFilter="/afs/cern.ch/cms/CAF/CMSCOMM/COMM_DQM/certification/Collisions18/13TeV/DCSOnly/json_DCSONLY.txt"
JsonFilter="/afs/cern.ch/cms/CAF/CMSCOMM/COMM_DQM/certification/Collisions17/13TeV/ReReco/Cert_294927-306462_13TeV_EOY2017ReReco_Collisions17_JSON.txt"

ecalproFolder="${CMSSW_BASE}/src/CalibCode/submit/"
outputdir="${ecalproFolder}InputList/"
outputfile="${dataset}_Run${runYear}_${dayMonthYear}.list"
fileList="${outputdir}${outputfile}"

cd ${ecalproFolder}
eval `scramv1 runtime -sh`

host=`echo "$HOSTNAME"`
if [[ ${host} != *"lxplus"* ]]; then
    echo "Error! You must be on lxplus to get list of files. Do ssh -XY lxplus and work from a release."
    return 0
fi

echo "Creating list of file running dasgoclient"
dasgoclient -query="file dataset=/${dataset}/Run${runYear}*/RAW" > ${fileList}
echo "List created in ${fileList}"

echo "Filtering list of files with json ${JsonFilter}"
purifyCmd="python Utilities/Purify_List.py ${fileList} ${JsonFilter}"
echo "${purifyCmd}"
echo "${purifyCmd}" | bash

echo "DONE!" 