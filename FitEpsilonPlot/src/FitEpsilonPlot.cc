// -*- C++ -*-
//
// Package:    FitEpsilonPlot
// Class:      FitEpsilonPlot
// 
/**\class FitEpsilonPlot FitEpsilonPlot.cc CalibCode/FitEpsilonPlot/src/FitEpsilonPlot.cc

Description: [one line class summary]

Implementation:
[Notes on implementation]
 */
//
// Original Author:  Marco Grassi, CMS
//         Created:  Tue Nov  8 17:18:54 CET 2011
// $Id: FitEpsilonPlot.cc,v 1.9 2013/06/17 13:40:42 lpernie Exp $
//
//

// system include files
#include <memory>
#include <iostream>
#include <string>

#include "TF1.h"
#include "TH1F.h"
#include "TH2F.h"
#include "TFile.h"
#include "TTree.h"
#include "TLatex.h"
#include "TMath.h"
#include "TCanvas.h"
#include "TPaveStats.h"
#include "TFitResult.h"
#include "TLegend.h"
#include "TROOT.h"
#include "TStyle.h"

// user include files
#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/EDAnalyzer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/Utilities/interface/Exception.h"

#include "DataFormats/EcalDetId/interface/EBDetId.h"
#include "DataFormats/EcalDetId/interface/EEDetId.h"

#include "RooGaussian.h"
#include "RooCBShape.h"
#include "RooChebychev.h"
#include "RooPolynomial.h"
#include "RooDataHist.h"
#include "RooAbsPdf.h"
#include "RooAddPdf.h"
#include "RooArgSet.h"
#include "RooArgList.h"
#include "RooPlot.h"
#include "RooFitResult.h"
#include "RooNLLVar.h"
#include "RooChi2Var.h"
#include "RooMinuit.h"
#include "RooMinimizer.h"
#include "RooAbsReal.h" 
#include "RooAbsCategory.h" 

#include "CalibCode/FitEpsilonPlot/interface/FitEpsilonPlot.h"

using std::cout;
using std::endl;
using std::string;
using namespace RooFit;


// in this code the upper boundary of mass for the fit is used to assess the goodness of fit: look for --> if( fitres.chi2 < 5 && fabs(mean-<some_number>)>0.0000001)
// which means "if the Chi2 is good and the mean of the fit is far from the upper boundary ..."
// the upper boundary must be consistent with <some_number>
static double upper_bound_pi0mass_EB = 0.15;
static double upper_bound_pi0mass_EE = 0.16;
static double upper_bound_etamass_EB = 0.62;
static double upper_bound_etamass_EE = 0.62;

static float fitRange_low_pi0 = 0.08; // value used in the fit function to define the fit range
static float fitRange_high_pi0 = 0.21; // value used in the fit function to define the fit range
static float fitRange_high_pi0_ext = 0.222;

static float fitRange_low_eta = 0.4; // value used in the fit function to define the fit range
static float fitRange_high_eta = 0.65; // value used in the fit function to define the fit range
static float fitRange_high_eta_ext = 0.67;

static float EoverEtrue_integralMin = 25; // require that integral in a given range is > EoverEtrue_integralMin for E/Etrue distribution (used for MC only)

FitEpsilonPlot::FitEpsilonPlot(const edm::ParameterSet& iConfig)

{

    //now do what ever initialization is needed
    currentIteration_ =  iConfig.getUntrackedParameter<int>("CurrentIteration");
    epsilonPlotFileName_ = iConfig.getUntrackedParameter<std::string>("EpsilonPlotFileName");
    outputDir_ = iConfig.getUntrackedParameter<std::string>("OutputDir");
    outfilename_          = iConfig.getUntrackedParameter<std::string>("OutputFile");
    calibMapPath_ = iConfig.getUntrackedParameter<std::string>("calibMapPath");
    inRangeFit_ = iConfig.getUntrackedParameter<int>("NInFit");
    finRangeFit_ = iConfig.getUntrackedParameter<int>("NFinFit");    
    EEoEB_ = iConfig.getUntrackedParameter<std::string>("EEorEB");
    isNot_2010_ = iConfig.getUntrackedParameter<bool>("isNot_2010");
    Are_pi0_ = iConfig.getUntrackedParameter<bool>("Are_pi0");
    StoreForTest_ = iConfig.getUntrackedParameter<bool>("StoreForTest",true);
    Barrel_orEndcap_ = iConfig.getUntrackedParameter<std::string>("Barrel_orEndcap");
    useMassInsteadOfEpsilon_ = iConfig.getUntrackedParameter<bool>("useMassInsteadOfEpsilon",true);
    isEoverEtrue_ = iConfig.getUntrackedParameter<bool>("isEoverEtrue",false);
    useFit_RooMinuit_ = iConfig.getUntrackedParameter<bool>("useFit_RooMinuit",false);
    foldInSuperModule_ = iConfig.getUntrackedParameter<bool>("foldInSuperModule",false);

    //foldInSuperModule_ = true;
    fitEoverEtrueWithRooFit_ = true;   // use bare TH1::Fit or RooFit (better, can stay true)
    readFoldedHistogramFromFile_ = false;  // read directly folded histograms (the folding is done in this analyzer, so the very first time this option is false)
    foldEB_all0_onlyPlus1_onlyMinus2_ = 0; // 0 to put all 36 SM in one, 1 for using EB+ only, 2 for using EB- only (but then they are used on all barrel because I only have a single SM map)

    // I should add a code that do the folding before going to the fitting part

    fitFileName_ = outfilename_;
    std::string strToReplace = "calibMap";
    fitFileName_.replace(outfilename_.find(strToReplace.c_str()),strToReplace.size(),"fitRes");
    fitFileName_ = outputDir_ + "/" + fitFileName_;

    inputEpsilonFile_ = nullptr; //  before we open the file, we assign a null value
    outfile_ = nullptr;

    /// setting calibration type
    calibTypeString_ = iConfig.getUntrackedParameter<std::string>("CalibType");
    if(     calibTypeString_.compare("xtal")    == 0 ) {
      calibTypeNumber_ = xtal;
      regionalCalibration_ = &xtalCalib;
      if (isEoverEtrue_) regionalCalibration_g2_ = new EcalRegionalCalibration<EcalCalibType::Xtal>();  //regionalCalibration_g2_ = &xtalCalib_g2;
    } else if(calibTypeString_.compare("tt")      == 0 ) {
      calibTypeNumber_ = tt;
      regionalCalibration_ = &TTCalib;
      if (isEoverEtrue_) regionalCalibration_g2_ = new EcalRegionalCalibration<EcalCalibType::TrigTower>(); // regionalCalibration_g2_ = &TTCalib_g2;
    } else if(calibTypeString_.compare("etaring") == 0 ) {
      calibTypeNumber_ = etaring;
      regionalCalibration_ = &etaCalib;
      if (isEoverEtrue_) regionalCalibration_g2_ = new EcalRegionalCalibration<EcalCalibType::EtaRing>(); // regionalCalibration_g2_ = &etaCalib_g2;
    } else throw cms::Exception("CalibType") << "Calib type not recognized\n";

    cout << "FIT_EPSILON: crosscheck: selected type: " << regionalCalibration_->printType() << endl;

    /// retrieving calibration coefficients of the previous iteration
    // if currentIteration_ = 0, calibMapPath_ contains "iter_-1" unless the current set of ICs was started from another existing set (see parameters.py)
    // therefore, the case with extension is included below
    std::string stringToMatch = "iter_-1";  // used below: this string should not match to trigger true condition 
    if(currentIteration_ < 0) throw cms::Exception("IterationNumber") << "Invalid negative iteration number\n";
    else if(currentIteration_ > 0 || (currentIteration_ == 0 && calibMapPath_.find(stringToMatch)==std::string::npos))
    {
      regionalCalibration_->getCalibMap()->loadCalibMapFromFile(calibMapPath_.c_str(),false);
      if (isEoverEtrue_) regionalCalibration_g2_->getCalibMap()->loadCalibMapFromFile(calibMapPath_.c_str(),true);
    }

    TH1::SetDefaultSumw2(); // all new histograms will automatically activate the storage of the sum of squares of errors (i.e, TH1::Sumw2 is automatically called).

    // load epsilon from current iter
    if (isEoverEtrue_) {

      if ((Barrel_orEndcap_=="ONLY_BARREL" || Barrel_orEndcap_=="ALL_PLEASE" )) {
	EoverEtrue_g1_EB_h = new TH1F*[regionalCalibration_->getCalibMap()->getNRegionsEB()];
	EoverEtrue_g2_EB_h = new TH1F*[regionalCalibration_g2_->getCalibMap()->getNRegionsEB()];
      }
      if ((Barrel_orEndcap_=="ONLY_ENDCAP" || Barrel_orEndcap_=="ALL_PLEASE" )) {
	EoverEtrue_g1_EE_h = new TH1F*[regionalCalibration_->getCalibMap()->getNRegionsEE()];
	EoverEtrue_g2_EE_h = new TH1F*[regionalCalibration_g2_->getCalibMap()->getNRegionsEE()];
      }
      cout << "FIT_EPSILON: FitEpsilonPlot:: loading EoverEtrue plots from file: " << epsilonPlotFileName_ << endl;
      loadEoverEtruePlot(epsilonPlotFileName_,1);
      loadEoverEtruePlot(epsilonPlotFileName_,2);

      if (foldInSuperModule_ && EEoEB_ == "Barrel" && (Barrel_orEndcap_=="ONLY_BARREL" || Barrel_orEndcap_=="ALL_PLEASE")) {

	EoverEtrue_g1_EB_SM_hvec.clear();
	EoverEtrue_g2_EB_SM_hvec.clear();

	// 20(phi)*85(ieta) crystals in 1 SM
	// we use a 1D vector ad treat it as a 2D array: the index convention is 
	// index = (fabs(ieta) - 1) + 85 * ((iphi - 1)%20)    ieta = 1, 2, ..., 85     iphi = 1, 2 , ..., 360 
	// ieta,iphi = 1,1 --> index = 0

	// ieta 1 --> 85
	//
	//  * * * * * * * * * * * * * *  iphi 1
	//  * * * * * * * . . . . . . .
	//  . . . . . . . . . . . . . . 
        //
	//  * * * * * * * . . . . . . .  iphi 20

	//cout << "EBDetId::kCrystalsPerSM = " << EBDetId::kCrystalsPerSM << endl;
	for (int iv = 0; iv < EBDetId::kCrystalsPerSM; ++iv) {  // 1700 crystals
	  
	    EoverEtrue_g1_EB_SM_hvec.push_back( new TH1F(Form("EoverEtrue_g1_EB_SM_hvec_%d",iv),
							 "g1 E/E_{true} folded in SM",
							 EoverEtrue_g1_EB_h[inRangeFit_]->GetNbinsX(),
							 EoverEtrue_g1_EB_h[inRangeFit_]->GetBinLowEdge(1),
							 EoverEtrue_g1_EB_h[inRangeFit_]->GetBinLowEdge(1+EoverEtrue_g1_EB_h[inRangeFit_]->GetNbinsX())
							 ) );
	    EoverEtrue_g2_EB_SM_hvec.push_back( new TH1F(Form("EoverEtrue_g2_EB_SM_hvec_%d",iv),
							 "g2 E/E_{true} folded in SM",
							 EoverEtrue_g2_EB_h[inRangeFit_]->GetNbinsX(),
							 EoverEtrue_g2_EB_h[inRangeFit_]->GetBinLowEdge(1),
							 EoverEtrue_g2_EB_h[inRangeFit_]->GetBinLowEdge(1+EoverEtrue_g2_EB_h[inRangeFit_]->GetNbinsX())
							 ) );

	}

	if (readFoldedHistogramFromFile_) {
	  cout << "FIT_EPSILON: reading folded histogram from file" << endl;
	  loadEoverEtruePlotFoldedInSM(1);
	  loadEoverEtruePlotFoldedInSM(2);
	} else {
	  cout << "FIT_EPSILON: folding histograms ..." << endl;
	  addHistogramsToFoldSM(EoverEtrue_g1_EB_SM_hvec,epsilonPlotFileName_,1);
	  addHistogramsToFoldSM(EoverEtrue_g2_EB_SM_hvec,epsilonPlotFileName_,2);
	  cout << "FIT_EPSILON: folding histograms completed..." << endl;
	}

      }

    } else {

      if ((Barrel_orEndcap_=="ONLY_BARREL" || Barrel_orEndcap_=="ALL_PLEASE" )) {
	epsilon_EB_h = new TH1F*[regionalCalibration_->getCalibMap()->getNRegionsEB()];
      }
      if ((Barrel_orEndcap_=="ONLY_ENDCAP" || Barrel_orEndcap_=="ALL_PLEASE" )) {
	epsilon_EE_h = new TH1F*[regionalCalibration_->getCalibMap()->getNRegionsEE()];
      }
      cout << "FIT_EPSILON: FitEpsilonPlot:: loading epsilon plots from file: " << epsilonPlotFileName_ << endl;
      loadEpsilonPlot(epsilonPlotFileName_);

      if (foldInSuperModule_ && EEoEB_ == "Barrel" && (Barrel_orEndcap_=="ONLY_BARREL" || Barrel_orEndcap_=="ALL_PLEASE")) {

	epsilon_EB_SM_hvec.clear();

	// 20(phi)*85(ieta) crystals in 1 SM
	// we use a 1D vector ad treat it as a 2D array: the index convention is 
	// index = (fabs(ieta) - 1) + 85 * ((iphi - 1)%20)    ieta = 1, 2, ..., 85     iphi = 1, 2 , ..., 360 
	// ieta,iphi = 1,1 --> index = 0

	// ieta 1 --> 85
	//
	//  * * * * * * * * * * * * * *  iphi 1
	//  * * * * * * * . . . . . . .
	//  . . . . . . . . . . . . . . 
        //
	//  * * * * * * * . . . . . . .  iphi 20

	//cout << "EBDetId::kCrystalsPerSM = " << EBDetId::kCrystalsPerSM << endl;
	for (int iv = 0; iv < EBDetId::kCrystalsPerSM; ++iv) {  // 1700 crystals
	  
	  epsilon_EB_SM_hvec.push_back( new TH1F(Form("epsilon_EB_SM_hvec_%d",iv),
						 "#pi^{0} mass folded in SM",
						 epsilon_EB_h[inRangeFit_]->GetNbinsX(),
						 epsilon_EB_h[inRangeFit_]->GetBinLowEdge(1),
						 epsilon_EB_h[inRangeFit_]->GetBinLowEdge(1+epsilon_EB_h[inRangeFit_]->GetNbinsX())
						 ) );

	}

	if (readFoldedHistogramFromFile_) {
	  cout << "FIT_EPSILON: reading folded histogram from file" << endl;
	  loadEpsilonPlotFoldedInSM();
	} else {
	  cout << "FIT_EPSILON: folding histograms ..." << endl;
	  addHistogramsToFoldSM(epsilon_EB_SM_hvec,epsilonPlotFileName_,1);
	  cout << "FIT_EPSILON: folding histograms completed..." << endl;
	}

      }

    }

}


FitEpsilonPlot::~FitEpsilonPlot()
{

  if ((Barrel_orEndcap_=="ONLY_BARREL" || Barrel_orEndcap_=="ALL_PLEASE" )) {

    if (isEoverEtrue_) {
      deleteEpsilonPlot(EoverEtrue_g1_EB_h, regionalCalibration_->getCalibMap()->getNRegionsEB() );
      deleteEpsilonPlot(EoverEtrue_g2_EB_h, regionalCalibration_g2_->getCalibMap()->getNRegionsEB() );
      // delete EoverEtrue_g1_EB_h;
      // delete EoverEtrue_g2_EB_h;
      if (foldInSuperModule_) {
	for (unsigned int i = 0; i < EoverEtrue_g1_EB_SM_hvec.size(); ++i) {
	  delete EoverEtrue_g1_EB_SM_hvec[i];
	  delete EoverEtrue_g2_EB_SM_hvec[i];
	}
	EoverEtrue_g1_EB_SM_hvec.clear();
	EoverEtrue_g2_EB_SM_hvec.clear();
      }
    } else {
      deleteEpsilonPlot(epsilon_EB_h, regionalCalibration_->getCalibMap()->getNRegionsEB() );
      if (foldInSuperModule_) {
	for (unsigned int i = 0; i < epsilon_EB_SM_hvec.size(); ++i) {
	  delete epsilon_EB_SM_hvec[i];
	}
	epsilon_EB_SM_hvec.clear();
      }
      // delete epsilon_EB_h;
    }

  }

  if ((Barrel_orEndcap_=="ONLY_ENDCAP" || Barrel_orEndcap_=="ALL_PLEASE" )) {

    if (isEoverEtrue_) {
      deleteEpsilonPlot(EoverEtrue_g1_EE_h, regionalCalibration_->getCalibMap()->getNRegionsEE() );
      deleteEpsilonPlot(EoverEtrue_g2_EE_h, regionalCalibration_g2_->getCalibMap()->getNRegionsEE() );
      // delete EoverEtrue_g1_EE_h;
      // delete EoverEtrue_g2_EE_h;
    } else {
      deleteEpsilonPlot(epsilon_EE_h, regionalCalibration_->getCalibMap()->getNRegionsEE() );
      // delete epsilon_EE_h;
    }

  }

  if (inputEpsilonFile_->IsOpen())
    inputEpsilonFile_->Close();

}


//
// member functions
//

int FitEpsilonPlot::getArrayIndexOfFoldedSMfromIetaIphi(const int ieta = 1, const int iphi = 1) {

  // note that the index in SM returned by this function in not the same as the index returned by EBDetId::ic()
  // the difference is mainly in the folding of EB+ on EB-
  // In our case, we overlay crystals such that ieta,iphi=20,40 goes on ieta,iphi=-20,40, 
  // i.e. the iphi coordinate is preserved when we consider two facing SM in EB+ and EB-
  // The usual CMSSW numbering scheme for crystals in SM is such that, looking at the center of the barrel, the crystal number 1 is always on the left
  // which means that the folding would overlay ieta,iphi=20,40 on ieta,iphi=-20,21
  
  // first 85 crystals correspond to iphi = 1 (in a SM)
  return (fabs(ieta) - 1) + EBDetId::kCrystalsInEta * ((iphi - 1) % EBDetId::kCrystalsInPhi);
  
}


int FitEpsilonPlot::getArrayIndexOfFoldedSMfromDenseIndex(const int index = 1, const bool useEBDetId_ic_scheme = true) {

  // when using EBDetId::ic(), the crystal number in SM is given such that ic=1 has iphi=1 in EB+ and iphi=20 in EB- (or the opposite, I don't remember)
  // the idea is that with ic() the crystal number is increased going from left to right

  EBDetId thisEBcrystal(EBDetId::detIdFromDenseIndex( index ));
  if (useEBDetId_ic_scheme) return thisEBcrystal.ic()-1;
  else                      return getArrayIndexOfFoldedSMfromIetaIphi(thisEBcrystal.ietaAbs(),thisEBcrystal.iphi());
  //return getArrayIndexOfFoldedSMfromIetaIphi(thisEBcrystal.ietaAbs(),thisEBcrystal.iphi());
}


void FitEpsilonPlot::addHistogramsToFoldSM(std::vector<TH1F*>& hvec, const std::string& filename, const int whichPhoton = 1) {

  if (hvec.size() == 0) throw cms::Exception("addHistogramsToFoldSM") << "Vector passed to function has size 0\n"; 

  std::string line = "";
  std::string histoNamePattern = isEoverEtrue_ ? Form("%s/EoverEtrue_g%d",EEoEB_.c_str(),whichPhoton) : Form("%s/epsilon",EEoEB_.c_str());

  // open the file if it has not been created so far, otherwise check that it is still open (this would happen on second photon)
  if (inputEpsilonFile_ == nullptr) {
    inputEpsilonFile_ = TFile::Open(filename.c_str(),"READ");
    if(!inputEpsilonFile_) 
      throw cms::Exception("addHistogramsToFoldSM") << "Cannot open file " << filename << "\n"; 
  } else if (not inputEpsilonFile_->IsOpen()) {
    inputEpsilonFile_ = TFile::Open(filename.c_str(),"READ");
  }

  ////////////////////
  // CAVEAT !!
  // If opening the following file, before writing objects we should do TFile::cd() (with the other files where histograms are saved)
  // This is because Root changes the current directory and messes up the filesystem
  //////////////////

  // // create file containing folded histograms (could be used later without folding again)
  // string foldFileName = filename;
  // std::string strToReplace = "epsilonPlots";
  // foldFileName.replace(filename.find(strToReplace.c_str()),strToReplace.size(),"histograms_foldedInSM");

  // string foldFileOpeningMode = (whichPhoton == 1) ? "RECREATE" : "UPDATE";
  // TFile* f = TFile::Open(foldFileName.c_str(),foldFileOpeningMode.c_str());
  // if (!f || !f->IsOpen()) {
  //   throw cms::Exception("FitEpsilonPlot") << "error opening file '" << foldFileName << "' to save folded histogram\n";
  // }

  TH1F* htmp = nullptr;

  // if we are here it means we are already in EB, but let's ask again
  if ( EEoEB_ == "Barrel" && (Barrel_orEndcap_=="ONLY_BARREL" || Barrel_orEndcap_=="ALL_PLEASE" ) ) {

    int nRegionsEB = ((whichPhoton == 1) ? regionalCalibration_->getCalibMap()->getNRegionsEB() : regionalCalibration_g2_->getCalibMap()->getNRegionsEB()); 
    
    for (int iR = 0; iR < nRegionsEB; ++iR) {

      EBDetId thisEBcrystal(EBDetId::detIdFromDenseIndex( iR));
      if (foldEB_all0_onlyPlus1_onlyMinus2_ == 1 && thisEBcrystal.ieta() < 0) continue; // if we want to use only EB+
      if (foldEB_all0_onlyPlus1_onlyMinus2_ == 2 && thisEBcrystal.ieta() > 0) continue; // if we want to use only EB-

      line = Form("%s_EB_iR_%d",histoNamePattern.c_str(), iR);
      //if (isTest) line = histoNamePattern;

      htmp = (TH1F*)inputEpsilonFile_->Get(line.c_str());      
      if(!htmp)	throw cms::Exception("addHistogramsToFoldSM") << "FIT_EPSILON: cannot load histogram " << line << "\n";
      
      int crystalIndexInSM = getArrayIndexOfFoldedSMfromDenseIndex(iR);
      if (crystalIndexInSM >= EBDetId::kCrystalsPerSM) {
	std::cout << "FIT_EPSILON: error in SM folding, index = " << crystalIndexInSM << std::endl;
	throw cms::Exception("FitEpsilonPlot") << "crystalIndexInSM >= " << EBDetId::kCrystalsPerSM << "\n";
      }
      if (htmp->GetEntries() > 0) {
	bool AddWasSuccesful = hvec[crystalIndexInSM]->Add(htmp);
	if (not AddWasSuccesful) throw cms::Exception("addHistogramsToFoldSM") << "FIT_EPSILON: failed to add histogram " << line << "\n";
	//if (crystalIndexInSM == 0) std::cout << "EoverEtrue_g1_EB_SM_hvec[0]->Integral = " << hvec[crystalIndexInSM] << std::endl;
      }

    }

  }

  // // save folded histogrmas
  // f->cd();
  // for (unsigned int i = 0; i < hvec.size(); i++) {
  //   hvec[i]->Write();
  // }
  // f->Close();

}

void FitEpsilonPlot::loadEoverEtruePlot(const std::string& filename, const int whichPhoton = 1) {

  // here regionalCalibration_ is only used to get the number of regions, which is the same for both photons
  // hence, no need to use regionalCalibration_ or regionalCalibration_g2_

  std::string line = "";
  std::string histoNamePattern = Form("%s/EoverEtrue_g%d",EEoEB_.c_str(),whichPhoton );

  // test the machinary fitting inclusive histogram (otherwise I have no statistics)
  // bool isTest = true;
  // if (isTest) {
  //   if (whichPhoton == 1) {
  //     if (EEoEB_ == "Barrel") histoNamePattern = "allEoverEtrue_g1_EB";
  //     else histoNamePattern = "allEoverEtrue_g1_EE";
  //   } else {
  //     if (EEoEB_ == "Barrel") histoNamePattern = "allEoverEtrue_g2_EB";
  //     else histoNamePattern = "allEoverEtrue_g2_EE";
  //   }
  // }

  // open the file if it has not been created so far, otherwise check that it is still open (this would happen on second photon)
  if (inputEpsilonFile_ == nullptr) {
    inputEpsilonFile_ = TFile::Open(filename.c_str());
    if(!inputEpsilonFile_) 
      throw cms::Exception("loadEpsilonPlot") << "Cannot open file " << filename << "\n"; 
  } else if (not inputEpsilonFile_->IsOpen()) {
    inputEpsilonFile_ = TFile::Open(filename.c_str());
  }

  if ( EEoEB_ == "Barrel" && (Barrel_orEndcap_=="ONLY_BARREL" || Barrel_orEndcap_=="ALL_PLEASE" ) ) {
    
    for (int iR=inRangeFit_; iR <= finRangeFit_ && iR < regionalCalibration_->getCalibMap()->getNRegionsEB(); iR++) {

      line = Form("%s_EB_iR_%d",histoNamePattern.c_str(), iR);
      //if (isTest) line = histoNamePattern;

      if (whichPhoton == 1) {
	EoverEtrue_g1_EB_h[iR] = (TH1F*)inputEpsilonFile_->Get(line.c_str());      
	if(!EoverEtrue_g1_EB_h[iR])
	  throw cms::Exception("loadEoverEtruePlot") << "Cannot load histogram " << line << "\n";
	else if(!(iR%1000))
	  cout << "FIT_EPSILON: EoverEtrue distribution (photon " << whichPhoton << ") for EB region " << iR << " loaded" << endl;
      } else {
	EoverEtrue_g2_EB_h[iR] = (TH1F*)inputEpsilonFile_->Get(line.c_str());      
	if(!EoverEtrue_g2_EB_h[iR])
	  throw cms::Exception("loadEoverEtruePlot") << "Cannot load histogram " << line << "\n";
	else if(!(iR%1000))
	  cout << "FIT_EPSILON: EoverEtrue distribution (photon " << whichPhoton << ") for EB region " << iR << " loaded" << endl;
      }
      
    }

  } else if( EEoEB_ == "Endcap" && (Barrel_orEndcap_=="ONLY_ENDCAP" || Barrel_orEndcap_=="ALL_PLEASE" ) ) {

    for (int jR=inRangeFit_; jR <= finRangeFit_ && jR<EEDetId::kSizeForDenseIndexing; jR++) {
      
      line = Form("%s_EE_iR_%d",histoNamePattern.c_str(), jR);
      //if (isTest) line = histoNamePattern;

      if (whichPhoton == 1) {

	EoverEtrue_g1_EE_h[jR] = (TH1F*)inputEpsilonFile_->Get(line.c_str());
	if(!EoverEtrue_g1_EE_h[jR])
	  throw cms::Exception("loadEoverEtruePlot") << "Cannot load histogram " << line << "\n";
	else if(!(jR%1000))
	  cout << "FIT_EPSILON: EoverEtrue distribution (photon " << whichPhoton << ") for EE region " << jR << " loaded" << endl;

      } else {

	EoverEtrue_g2_EE_h[jR] = (TH1F*)inputEpsilonFile_->Get(line.c_str());
	if(!EoverEtrue_g2_EE_h[jR])
	  throw cms::Exception("loadEoverEtruePlot") << "Cannot load histogram " << line << "\n";
	else if(!(jR%1000))
	  cout << "FIT_EPSILON: EoverEtrue distribution (photon " << whichPhoton << ") for EE region " << jR << " loaded" << endl;
	
      }

    }

  }

}

//============================================================

void FitEpsilonPlot::loadEoverEtruePlotFoldedInSM(const int whichPhoton = 1) {

  string foldFileName = epsilonPlotFileName_;
  std::string strToReplace = "epsilonPlots";
  foldFileName.replace(epsilonPlotFileName_.find(strToReplace.c_str()),strToReplace.size(),"histograms_foldedInSM");
  std::string line = "";
  std::string histoNamePattern = Form("EoverEtrue_g%d_EB_SM_hvec",whichPhoton );

  TFile* fileFoldHistogram  = TFile::Open(foldFileName.c_str(),"READ");
  if(!fileFoldHistogram) 
    throw cms::Exception("loadEoverEtruePlotFoldedInSM") << "Cannot open file " << foldFileName << "\n"; 

  if ( EEoEB_ == "Barrel" && (Barrel_orEndcap_=="ONLY_BARREL" || Barrel_orEndcap_=="ALL_PLEASE" ) ) {
    
    for (int iR=inRangeFit_; iR <= finRangeFit_ && iR < regionalCalibration_->getCalibMap()->getNRegionsEB(); iR++) {

      int indexSM = getArrayIndexOfFoldedSMfromDenseIndex(iR);
      line = Form("%s_%d",histoNamePattern.c_str(), indexSM);

      if (whichPhoton == 1) {
	EoverEtrue_g1_EB_SM_hvec[indexSM] = (TH1F*)fileFoldHistogram->Get(line.c_str());      
	if(!EoverEtrue_g1_EB_SM_hvec[indexSM])
	  throw cms::Exception("loadEoverEtruePlot") << "Cannot load histogram " << line << "\n";
	else {
	  EoverEtrue_g1_EB_SM_hvec[indexSM]->SetDirectory(0);
	  if(!(iR%1000))
	    cout << "FIT_EPSILON: EoverEtrue distribution (photon " << whichPhoton << ") for EB region " << iR << " loaded" << endl;
	}
      } else {
	EoverEtrue_g2_EB_SM_hvec[indexSM] = (TH1F*)fileFoldHistogram->Get(line.c_str());      
	if(!EoverEtrue_g2_EB_SM_hvec[indexSM])
	  throw cms::Exception("loadEoverEtruePlot") << "Cannot load histogram " << line << "\n";
	else {
	  EoverEtrue_g2_EB_SM_hvec[indexSM]->SetDirectory(0);
	  if(!(iR%1000))
	    cout << "FIT_EPSILON: EoverEtrue distribution (photon " << whichPhoton << ") for EB region " << iR << " loaded" << endl;
	}
      }

    }

  }
  
  fileFoldHistogram->Close();

}

//============================================================

void FitEpsilonPlot::loadEpsilonPlotFoldedInSM() {

  string foldFileName = epsilonPlotFileName_;
  std::string strToReplace = "epsilonPlots";
  foldFileName.replace(epsilonPlotFileName_.find(strToReplace.c_str()),strToReplace.size(),"histograms_foldedInSM");
  std::string line = "";
  std::string histoNamePattern = "epsilon_EB_SM_hvec";

  TFile* fileFoldHistogram  = TFile::Open(foldFileName.c_str(),"READ");
  if(!fileFoldHistogram) 
    throw cms::Exception("loadEpsilonPlotFoldedInSM") << "Cannot open file " << foldFileName << "\n"; 

  if ( EEoEB_ == "Barrel" && (Barrel_orEndcap_=="ONLY_BARREL" || Barrel_orEndcap_=="ALL_PLEASE" ) ) {
    
    for (int iR=inRangeFit_; iR <= finRangeFit_ && iR < regionalCalibration_->getCalibMap()->getNRegionsEB(); iR++) {

      int indexSM = getArrayIndexOfFoldedSMfromDenseIndex(iR);
      line = Form("%s_%d",histoNamePattern.c_str(), indexSM);

      epsilon_EB_SM_hvec[indexSM] = (TH1F*)fileFoldHistogram->Get(line.c_str());      
      if(!epsilon_EB_SM_hvec[indexSM])
	throw cms::Exception("loadEpsilonPlot") << "Cannot load histogram " << line << "\n";
      else {
	epsilon_EB_SM_hvec[indexSM]->SetDirectory(0);
	if(!(iR%1000))
	  cout << "FIT_EPSILON: epsilon distribution for EB region " << iR << " loaded" << endl;
      }

    }

  }
  
  fileFoldHistogram->Close();

}

//============================================================

void FitEpsilonPlot::loadEpsilonPlot(const std::string& filename)
{
  std::string line = "";

  inputEpsilonFile_ = TFile::Open(filename.c_str());
  if(!inputEpsilonFile_) 
    throw cms::Exception("loadEpsilonPlot") << "Cannot open file " << filename << "\n"; 
  if( EEoEB_ == "Barrel" && (Barrel_orEndcap_=="ONLY_BARREL" || Barrel_orEndcap_=="ALL_PLEASE" ) ){
    for(int iR=inRangeFit_; iR <= finRangeFit_ && iR < regionalCalibration_->getCalibMap()->getNRegionsEB(); iR++)
      {
	line = Form("Barrel/epsilon_EB_iR_%d",iR);
	epsilon_EB_h[iR] = (TH1F*)inputEpsilonFile_->Get(line.c_str());

	if(!epsilon_EB_h[iR])
	  throw cms::Exception("loadEpsilonPlot") << "Cannot load histogram " << line << "\n";
	else if(!(iR%1000))
	  cout << "FIT_EPSILON: Epsilon distribution for EB region " << iR << " loaded" << endl;
      }
  }
  else if( EEoEB_ == "Endcap" && (Barrel_orEndcap_=="ONLY_ENDCAP" || Barrel_orEndcap_=="ALL_PLEASE" ) ){
    for(int jR=inRangeFit_; jR <= finRangeFit_ && jR<EEDetId::kSizeForDenseIndexing; jR++)
      {
	line = Form("Endcap/epsilon_EE_iR_%d",jR);
	epsilon_EE_h[jR] = (TH1F*)inputEpsilonFile_->Get(line.c_str());
	if(!epsilon_EE_h[jR])
	  throw cms::Exception("loadEpsilonPlot") << "Cannot load histogram " << line << "\n";
	else if(!(jR%1000))
	  cout << "FIT_EPSILON: Epsilon distribution for EE region " << jR << " loaded" << endl;
      }
  }

}



void  FitEpsilonPlot::deleteEpsilonPlot(TH1F **h, int size)
{
    for(int jR=0; jR<size; jR++)
	  delete h[jR];

    delete h;
}


void FitEpsilonPlot::saveCoefficients() 
{
  /// output file
  std::string fileName = outputDir_  + "/" + outfilename_;
  outfile_ = new TFile(fileName.c_str(),"RECREATE");
  cout << "FIT_EPSILON: Saving Calibration Coefficients in " << fileName << " ... " << endl;;
  if(!outfile_) throw cms::Exception("WritingOutputFile") << "It was no possible to create output file " << fileName << "\n";
  outfile_->cd();

  // 2D calib map in the barrel
  TH2F* hmap_EB = new TH2F("calibMap_EB","EB calib coefficients: #eta on x, #phi on y",
			   2*EBDetId::MAX_IETA+1,-EBDetId::MAX_IETA-0.5,EBDetId::MAX_IETA+0.5,
			   EBDetId::MAX_IPHI, EBDetId::MIN_IPHI-0.5, EBDetId::MAX_IPHI+0.5 );
  hmap_EB->GetXaxis()->SetTitle("i#eta");
  hmap_EB->GetYaxis()->SetTitle("i#phi");
  TH2F* hmap_EEp = new TH2F("calibMap_EEp","EE+ calib coefficients",100,0.5,100.5,100,0.5,100.5);
  hmap_EEp->GetXaxis()->SetTitle("ix");
  hmap_EEp->GetYaxis()->SetTitle("iy");
  TH2F* hmap_EEm = new TH2F("calibMap_EEm","EE- calib coefficients",100,0.5,100.5,100,0.5,100.5);
  hmap_EEm->GetXaxis()->SetTitle("ix");
  hmap_EEm->GetYaxis()->SetTitle("iy");
  TH1F* hint = new TH1F("hint","Bin1: inRangeFit_ Bin2: finRangeFit_ Bin3: Barrel(0)/Endcap(1)",3,0.,3.);
  hint->SetBinContent(1,inRangeFit_);
  hint->SetBinContent(2,finRangeFit_);
  if( EEoEB_ == "Barrel" ) hint->SetBinContent(3,0);
  else                     hint->SetBinContent(3,1);
  hint->Write();

  /// filling Barrel Map
  for(int j=0; j<regionalCalibration_->getCalibMap()->getNRegionsEB(); ++j)  
    {
      std::vector<DetId> ids = regionalCalibration_->allDetIdsInEBRegion(j);
      for(std::vector<DetId>::const_iterator iid = ids.begin(); iid != ids.end(); ++iid) {
	EBDetId ebid(*iid);
	int ix = ebid.ieta()+EBDetId::MAX_IETA+1;

	float coeffValue = regionalCalibration_->getCalibMap()->coeff(*iid) > 0. ? regionalCalibration_->getCalibMap()->coeff(*iid) : 1.;
	hmap_EB->SetBinContent( ix, ebid.iphi(), coeffValue );
      } // loop over DetId in regions
    }
  hmap_EB->SetMinimum(0.9);
  hmap_EB->SetStats(false);
  hmap_EB->Write();

  for(int jR=0; jR < regionalCalibration_->getCalibMap()->getNRegionsEE(); jR++)
    {
      std::vector<DetId> ids =  regionalCalibration_->allDetIdsInEERegion(jR);
      for(std::vector<DetId>::const_iterator iid = ids.begin(); iid != ids.end(); ++iid) 
	{ 
	  EEDetId eeid(*iid);
	  float coeffValue =  regionalCalibration_->getCalibMap()->coeff(*iid) > 0. ?  regionalCalibration_->getCalibMap()->coeff(*iid) : 1.;

	  if(eeid.positiveZ())
	    hmap_EEp->Fill(eeid.ix(), eeid.iy(), coeffValue); 
	  else 
	    hmap_EEm->Fill(eeid.ix(), eeid.iy(), coeffValue);
	}
    }

  hmap_EEp->SetMinimum(0.9);
  hmap_EEp->SetStats(false);
  hmap_EEp->Write();

  hmap_EEm->SetMinimum(0.9);
  hmap_EEm->SetStats(false);
  hmap_EEm->Write();

  /*------------- TTREE --------------*/

  uint32_t   rawId;
  int        hashedIndex;
  int        ieta;
  int        iphi;
  int        iSM;
  int        iMod;
  int        iTT;
  int        iTTeta;
  int        iTTphi;
  int        iter = currentIteration_;
  float      regCoeff;
  float      Signal;//#
  float      Backgr; 
  float      Chisqu; 
  float      Ndof; 
  float      fit_mean;
  float      fit_mean_err;
  float      fit_sigma;
  float      fit_Snorm;
  float      fit_b0;
  float      fit_b1;    
  float      fit_b2;    
  float      fit_b3;    
  float      fit_Bnorm; 
  /// endcap variables
  int ix;
  int iy;
  int zside;
  int sc; 
  int isc;
  int ic;
  int iquadrant;

  TTree* treeEB = new TTree("calibEB","Tree of EB Inter-calibration constants");
  TTree* treeEE = new TTree("calibEE","Tree of EE Inter-calibration constants");


  /// barrel
  treeEB->Branch("rawId",&rawId,"rawId/i");
  treeEB->Branch("hashedIndex",&hashedIndex,"hashedIndex/I");
  treeEB->Branch("ieta",&ieta,"ieta/I");
  treeEB->Branch("iphi",&iphi,"iphi/I");
  treeEB->Branch("iSM",&iSM,"iSM/I");
  treeEB->Branch("iMod",&iMod,"iMod/I");
  treeEB->Branch("iTT",&iTT,"iTT/I");
  treeEB->Branch("iTTeta",&iTTeta,"iTTeta/I");
  treeEB->Branch("iTTphi",&iTTphi,"iTTphi/I");
  treeEB->Branch("iter",&iter,"iter/I");
  treeEB->Branch("coeff",&regCoeff,"coeff/F");
  treeEB->Branch("Signal",&Signal,"Signal/F");//#
  treeEB->Branch("Backgr",&Backgr,"Backgr/F");
  treeEB->Branch("Chisqu",&Chisqu,"Chisqu/F");
  treeEB->Branch("Ndof",&Ndof,"Ndof/F");
  treeEB->Branch("fit_mean",&fit_mean,"fit_mean/F");
  treeEB->Branch("fit_mean_err",&fit_mean_err,"fit_mean_err/F");
  treeEB->Branch("fit_sigma",&fit_sigma,"fit_sigma/F");
  treeEB->Branch("fit_Snorm",&fit_Snorm,"fit_Snorm/F");
  treeEB->Branch("fit_b0",&fit_b0,"fit_b0/F");
  treeEB->Branch("fit_b1",&fit_b1,"fit_b1/F");
  treeEB->Branch("fit_b2",&fit_b2,"fit_b2/F");
  treeEB->Branch("fit_b3",&fit_b3,"fit_b3/F");
  treeEB->Branch("fit_Bnorm",&fit_Bnorm,"fit_Bnorm/F");

  /// endcap
  treeEE->Branch("ix",&ix,"ix/I");
  treeEE->Branch("iy",&iy,"iy/I");
  treeEE->Branch("zside",&zside,"zside/I");
  treeEE->Branch("sc",&sc,"sc/I");
  treeEE->Branch("isc",&isc,"isc/I");
  treeEE->Branch("ic",&ic,"ic/I");
  treeEE->Branch("iquadrant",&iquadrant,"iquadrant/I");
  treeEE->Branch("hashedIndex",&hashedIndex,"hashedIndex/I");
  treeEE->Branch("iter",&iter,"iter/I");
  treeEE->Branch("coeff",&regCoeff,"coeff/F");
  treeEE->Branch("Signal",&Signal,"Signal/F");//#
  treeEE->Branch("Backgr",&Backgr,"Backgr/F");
  treeEE->Branch("Chisqu",&Chisqu,"Chisqu/F");
  treeEE->Branch("Ndof",&Ndof,"Ndof/F");
  treeEE->Branch("fit_mean",&fit_mean,"fit_mean/F");
  treeEE->Branch("fit_mean_err",&fit_mean_err,"fit_mean_err/F");
  treeEE->Branch("fit_sigma",&fit_sigma,"fit_sigma/F");
  treeEE->Branch("fit_Snorm",&fit_Snorm,"fit_Snorm/F");
  treeEE->Branch("fit_b0",&fit_b0,"fit_b0/F");
  treeEE->Branch("fit_b1",&fit_b1,"fit_b1/F");
  treeEE->Branch("fit_b2",&fit_b2,"fit_b2/F");
  treeEE->Branch("fit_b3",&fit_b3,"fit_b3/F");
  treeEE->Branch("fit_Bnorm",&fit_Bnorm,"fit_Bnorm/F");


  for(int iR=0; iR < regionalCalibration_->getCalibMap()->getNRegionsEB(); ++iR)  {
    std::vector<DetId> ids = regionalCalibration_->allDetIdsInEBRegion(iR);
    for(std::vector<DetId>::const_iterator iid = ids.begin(); iid != ids.end(); ++iid) {
      EBDetId ebid(*iid);
      hashedIndex = ebid.hashedIndex();
      ieta = ebid.ieta();
      iphi = ebid.iphi();
      iSM = ebid.ism();
      iMod = ebid.im();
      iTT  = ebid.tower().hashedIndex();
      iTTeta = ebid.tower_ieta();
      iTTphi = ebid.tower_iphi();
      Signal = EBmap_Signal[ebid.hashedIndex()];//#
      Backgr = EBmap_Backgr[ebid.hashedIndex()];
      Chisqu = EBmap_Chisqu[ebid.hashedIndex()];
      Ndof = EBmap_ndof[ebid.hashedIndex()];
      fit_mean     = EBmap_mean[ebid.hashedIndex()];
      fit_mean_err = EBmap_mean_err[ebid.hashedIndex()];
      fit_sigma  = EBmap_sigma[ebid.hashedIndex()];
      fit_Snorm  = EBmap_Snorm[ebid.hashedIndex()];
      fit_b0     = EBmap_b0[ebid.hashedIndex()];
      fit_b1     = EBmap_b1[ebid.hashedIndex()];
      fit_b2     = EBmap_b2[ebid.hashedIndex()];
      fit_b3     = EBmap_b3[ebid.hashedIndex()];
      fit_Bnorm  = EBmap_Bnorm[ebid.hashedIndex()];

      regCoeff = regionalCalibration_->getCalibMap()->coeff(*iid);

      treeEB->Fill();
    } // loop over DetId in regions
  } // loop over regions

  for(int jR=0; jR < regionalCalibration_->getCalibMap()->getNRegionsEE() ; jR++)
    {
      std::vector<DetId> ids = regionalCalibration_->allDetIdsInEERegion(jR);
      for(std::vector<DetId>::const_iterator iid = ids.begin(); iid != ids.end(); ++iid) 
	{ 
	  EEDetId eeid(*iid);
	  ix = eeid.ix();
	  iy = eeid.iy();
	  zside = eeid.zside();
	  sc = eeid.sc();
	  isc = eeid.isc();
	  ic = eeid.ic();
	  iquadrant = eeid.iquadrant();
	  hashedIndex = eeid.hashedIndex();
	  regCoeff = regionalCalibration_->getCalibMap()->coeff(*iid);
	  Signal = EEmap_Signal[eeid.hashedIndex()];//#
	  Backgr = EEmap_Backgr[eeid.hashedIndex()];
	  Chisqu = EEmap_Chisqu[eeid.hashedIndex()];            
	  Ndof = EEmap_ndof[eeid.hashedIndex()];            
	  fit_mean     = EEmap_mean[eeid.hashedIndex()];
	  fit_mean_err = EEmap_mean_err[eeid.hashedIndex()];
	  fit_sigma  = EEmap_sigma[eeid.hashedIndex()];
	  fit_Snorm  = EEmap_Snorm[eeid.hashedIndex()];
	  fit_b0     = EEmap_b0[eeid.hashedIndex()];
	  fit_b1     = EEmap_b1[eeid.hashedIndex()];
	  fit_b2     = EEmap_b2[eeid.hashedIndex()];
	  fit_b3     = EEmap_b3[eeid.hashedIndex()];
	  fit_Bnorm  = EEmap_Bnorm[eeid.hashedIndex()];

	  treeEE->Fill();
	}
    }

  treeEB->Write();
  treeEE->Write();

  outfile_->Write();
  outfile_->Close();
  cout << "FIT_EPSILON:  done" << endl;

}

//==========================

void FitEpsilonPlot::saveCoefficientsEoverEtrue(const bool isSecondGenPhoton = false) 
{

  // important, if using the second photon the output file is updated, so the call with isSecondGenPhoton = true should be made as the second one
  // otherwise, based on the current implementation, at the time you open the file for the first photon the file would be overwritten due to RECREATE mode

  /// output file
  std::string fileName = outputDir_  + "/" + outfilename_;
  if (isSecondGenPhoton) outfile_ = new TFile(fileName.c_str(),"UPDATE");
  else                   outfile_ = new TFile(fileName.c_str(),"RECREATE");
  cout << "FIT_EPSILON: Saving E/Etrue Coefficients in " << fileName << " ... " << endl;;
  if(!outfile_) throw cms::Exception("WritingOutputFile") << "It was no possible to create output file " << fileName << "\n";
  outfile_->cd();

  // 2D calib map in the barrel
  TH2F* hmap_EB = new TH2F((isSecondGenPhoton ? "calibMap_EB_g2" : "calibMap_EB"),"EB calib coefficients: #eta on x, #phi on y",
			   2*EBDetId::MAX_IETA+1,-EBDetId::MAX_IETA-0.5,EBDetId::MAX_IETA+0.5,
			   EBDetId::MAX_IPHI, EBDetId::MIN_IPHI-0.5, EBDetId::MAX_IPHI+0.5 );
  hmap_EB->GetXaxis()->SetTitle("i#eta");
  hmap_EB->GetYaxis()->SetTitle("i#phi");
  TH2F* hmap_EEp = new TH2F((isSecondGenPhoton ? "calibMap_EEp_g2" : "calibMap_EEp"),"EE+ calib coefficients",100,0.5,100.5,100,0.5,100.5);
  hmap_EEp->GetXaxis()->SetTitle("ix");
  hmap_EEp->GetYaxis()->SetTitle("iy");
  TH2F* hmap_EEm = new TH2F((isSecondGenPhoton ? "calibMap_EEm_g2" : "calibMap_EEm"),"EE- calib coefficients",100,0.5,100.5,100,0.5,100.5);
  hmap_EEm->GetXaxis()->SetTitle("ix");
  hmap_EEm->GetYaxis()->SetTitle("iy");
  TH1F* hint = new TH1F("hint","Bin1: inRangeFit_ Bin2: finRangeFit_ Bin3: Barrel(0)/Endcap(1)",3,0.,3.);
  hint->SetBinContent(1,inRangeFit_);
  hint->SetBinContent(2,finRangeFit_);
  if( EEoEB_ == "Barrel" ) hint->SetBinContent(3,0);
  else                     hint->SetBinContent(3,1);
  hint->Write();

  EcalRegionalCalibrationBase* regCalibToUse = (isSecondGenPhoton) ? regionalCalibration_g2_ : regionalCalibration_;
  std::map<int,TFitResultPtr>& EBmap_fitresptrToUse = (isSecondGenPhoton) ? EBmap_fitresptr_g2 : EBmap_fitresptr_g1;
  std::map<int,TFitResultPtr>& EEmap_fitresptrToUse = (isSecondGenPhoton) ? EEmap_fitresptr_g2 : EEmap_fitresptr_g1;

  //filling Barrel Map
  for(int j=0; j<regCalibToUse->getCalibMap()->getNRegionsEB(); ++j)  
    {
      std::vector<DetId> ids = regCalibToUse->allDetIdsInEBRegion(j);
      for(std::vector<DetId>::const_iterator iid = ids.begin(); iid != ids.end(); ++iid) {
  	EBDetId ebid(*iid);
  	int ix = ebid.ieta()+EBDetId::MAX_IETA+1;

  	float coeffValue = regCalibToUse->getCalibMap()->coeff(*iid) > 0. ? regCalibToUse->getCalibMap()->coeff(*iid) : 1.;
  	hmap_EB->SetBinContent( ix, ebid.iphi(), coeffValue );
      } // loop over DetId in regions
    }

  hmap_EB->SetMinimum(0.9);
  hmap_EB->SetStats(false);
  hmap_EB->Write();

  for(int jR=0; jR < regCalibToUse->getCalibMap()->getNRegionsEE(); jR++)
    {
      std::vector<DetId> ids = regCalibToUse->allDetIdsInEERegion(jR);
      for(std::vector<DetId>::const_iterator iid = ids.begin(); iid != ids.end(); ++iid) 
  	{ 
  	  EEDetId eeid(*iid);
  	  float coeffValue =  regCalibToUse->getCalibMap()->coeff(*iid) > 0. ?  regCalibToUse->getCalibMap()->coeff(*iid) : 1.;

  	  if(eeid.positiveZ())
  	    hmap_EEp->Fill(eeid.ix(), eeid.iy(), coeffValue); 
  	  else 
  	    hmap_EEm->Fill(eeid.ix(), eeid.iy(), coeffValue);
  	}
    }

  hmap_EEp->SetMinimum(0.9);
  hmap_EEp->SetStats(false);
  hmap_EEp->Write();

  hmap_EEm->SetMinimum(0.9);
  hmap_EEm->SetStats(false);
  hmap_EEm->Write();

  /*------------- TTREE --------------*/
  uint32_t   rawId;
  int        hashedIndex;
  int        ieta;
  int        iphi;
  int        iSM;
  int        iMod;
  int        iTT;
  int        iTTeta;
  int        iTTphi;
  int        iter = currentIteration_;
  float      regCoeff;
  // float      Signal;//#
  // float      Backgr; 
  float      Chisqu; 
  float      Ndof; 
  float      fit_mean;
  float      fit_mean_err;
  float      fit_sigma;
  // float      fit_Snorm;
  // float      fit_b0;
  // float      fit_b1;    
  // float      fit_b2;    
  // float      fit_b3;    
  // float      fit_Bnorm; 
  /// endcap variables
  int ix;
  int iy;
  int zside;
  int sc; 
  int isc;
  int ic;
  int iquadrant;

  TTree* treeEB = new TTree((isSecondGenPhoton ? "calibEB_g2" : "calibEB"),"Tree of EB Inter-calibration constants");
  TTree* treeEE = new TTree((isSecondGenPhoton ? "calibEE_g2" : "calibEE"),"Tree of EE Inter-calibration constants");

  /// barrel
  treeEB->Branch("rawId",&rawId,"rawId/i");
  treeEB->Branch("hashedIndex",&hashedIndex,"hashedIndex/I");
  treeEB->Branch("ieta",&ieta,"ieta/I");
  treeEB->Branch("iphi",&iphi,"iphi/I");
  treeEB->Branch("iSM",&iSM,"iSM/I");
  treeEB->Branch("iMod",&iMod,"iMod/I");
  treeEB->Branch("iTT",&iTT,"iTT/I");
  treeEB->Branch("iTTeta",&iTTeta,"iTTeta/I");
  treeEB->Branch("iTTphi",&iTTphi,"iTTphi/I");
  treeEB->Branch("iter",&iter,"iter/I");
  treeEB->Branch("coeff",&regCoeff,"coeff/F");
  // treeEB->Branch("Signal",&Signal,"Signal/F");//#
  // treeEB->Branch("Backgr",&Backgr,"Backgr/F");
  treeEB->Branch("Chisqu",&Chisqu,"Chisqu/F");
  treeEB->Branch("Ndof",&Ndof,"Ndof/F");
  treeEB->Branch("fit_mean",&fit_mean,"fit_mean/F");
  treeEB->Branch("fit_mean_err",&fit_mean_err,"fit_mean_err/F");
  treeEB->Branch("fit_sigma",&fit_sigma,"fit_sigma/F");
  // treeEB->Branch("fit_Snorm",&fit_Snorm,"fit_Snorm/F");
  // treeEB->Branch("fit_b0",&fit_b0,"fit_b0/F");
  // treeEB->Branch("fit_b1",&fit_b1,"fit_b1/F");
  // treeEB->Branch("fit_b2",&fit_b2,"fit_b2/F");
  // treeEB->Branch("fit_b3",&fit_b3,"fit_b3/F");
  // treeEB->Branch("fit_Bnorm",&fit_Bnorm,"fit_Bnorm/F");

  /// endcap
  treeEE->Branch("ix",&ix,"ix/I");
  treeEE->Branch("iy",&iy,"iy/I");
  treeEE->Branch("zside",&zside,"zside/I");
  treeEE->Branch("sc",&sc,"sc/I");
  treeEE->Branch("isc",&isc,"isc/I");
  treeEE->Branch("ic",&ic,"ic/I");
  treeEE->Branch("iquadrant",&iquadrant,"iquadrant/I");
  treeEE->Branch("hashedIndex",&hashedIndex,"hashedIndex/I");
  treeEE->Branch("iter",&iter,"iter/I");
  treeEE->Branch("coeff",&regCoeff,"coeff/F");
  // treeEE->Branch("Signal",&Signal,"Signal/F");//#
  // treeEE->Branch("Backgr",&Backgr,"Backgr/F");
  treeEE->Branch("Chisqu",&Chisqu,"Chisqu/F");
  treeEE->Branch("Ndof",&Ndof,"Ndof/F");
  treeEE->Branch("fit_mean",&fit_mean,"fit_mean/F");
  treeEE->Branch("fit_mean_err",&fit_mean_err,"fit_mean_err/F");
  treeEE->Branch("fit_sigma",&fit_sigma,"fit_sigma/F");
  // treeEE->Branch("fit_Snorm",&fit_Snorm,"fit_Snorm/F");
  // treeEE->Branch("fit_b0",&fit_b0,"fit_b0/F");
  // treeEE->Branch("fit_b1",&fit_b1,"fit_b1/F");
  // treeEE->Branch("fit_b2",&fit_b2,"fit_b2/F");
  // treeEE->Branch("fit_b3",&fit_b3,"fit_b3/F");
  // treeEE->Branch("fit_Bnorm",&fit_Bnorm,"fit_Bnorm/F");

  for(int iR=0; iR < regCalibToUse->getCalibMap()->getNRegionsEB(); ++iR)  {

    std::vector<DetId> ids = regCalibToUse->allDetIdsInEBRegion(iR);
    for(std::vector<DetId>::const_iterator iid = ids.begin(); iid != ids.end(); ++iid) {

      EBDetId ebid(*iid);
      hashedIndex = ebid.hashedIndex();
      ieta = ebid.ieta();
      iphi = ebid.iphi();
      iSM = ebid.ism();
      iMod = ebid.im();
      iTT  = ebid.tower().hashedIndex();
      iTTeta = ebid.tower_ieta();
      iTTphi = ebid.tower_iphi();
      // // Signal = EBmap_Signal[ebid.hashedIndex()];//#
      // // Backgr = EBmap_Backgr[ebid.hashedIndex()];
      // Chisqu = EBmap_Chisqu[ebid.hashedIndex()];
      // Ndof = EBmap_ndof[ebid.hashedIndex()];
      // fit_mean     = EBmap_mean[ebid.hashedIndex()];
      // fit_mean_err = EBmap_mean_err[ebid.hashedIndex()];
      // fit_sigma  = EBmap_sigma[ebid.hashedIndex()];
      // // fit_Snorm  = EBmap_Snorm[ebid.hashedIndex()];
      // // fit_b0     = EBmap_b0[ebid.hashedIndex()];
      // // fit_b1     = EBmap_b1[ebid.hashedIndex()];
      // // fit_b2     = EBmap_b2[ebid.hashedIndex()];
      // // fit_b3     = EBmap_b3[ebid.hashedIndex()];
      // // fit_Bnorm  = EBmap_Bnorm[ebid.hashedIndex()];

      if (EBmap_fitresptrToUse[ebid.hashedIndex()] >= 0 && EBmap_fitresptrToUse[ebid.hashedIndex()].Get() != nullptr) {
	Chisqu       = EBmap_fitresptrToUse[ebid.hashedIndex()]->Chi2();
	Ndof         = EBmap_fitresptrToUse[ebid.hashedIndex()]->Ndf();
	fit_mean     = EBmap_fitresptrToUse[ebid.hashedIndex()]->Parameter(1);  // for the double CB the mean is parameter [1] (as for the gaussian)
	fit_mean_err = EBmap_fitresptrToUse[ebid.hashedIndex()]->ParError(1);
	fit_sigma    = EBmap_fitresptrToUse[ebid.hashedIndex()]->Parameter(2);
      } else {
	Chisqu       = -999;
	Ndof         = -999;
	fit_mean     = -999;
	fit_mean_err = -999;
	fit_sigma    = -999;
      }

      regCoeff = regCalibToUse->getCalibMap()->coeff(*iid);

      treeEB->Fill();
    } // loop over DetId in regions
  } // loop over regions

  for(int jR=0; jR < regCalibToUse->getCalibMap()->getNRegionsEE() ; jR++) {

    std::vector<DetId> ids = regCalibToUse->allDetIdsInEERegion(jR);

    for(std::vector<DetId>::const_iterator iid = ids.begin(); iid != ids.end(); ++iid) { 

      EEDetId eeid(*iid);
      ix = eeid.ix();
      iy = eeid.iy();
      zside = eeid.zside();
      sc = eeid.sc();
      isc = eeid.isc();
      ic = eeid.ic();
      iquadrant = eeid.iquadrant();
      hashedIndex = eeid.hashedIndex();
      regCoeff = regCalibToUse->getCalibMap()->coeff(*iid);
      // // Signal = EEmap_Signal[eeid.hashedIndex()];//#
      // // Backgr = EEmap_Backgr[eeid.hashedIndex()];
      // Chisqu = EEmap_Chisqu[eeid.hashedIndex()];            
      // Ndof = EEmap_ndof[eeid.hashedIndex()];            
      // fit_mean     = EEmap_mean[eeid.hashedIndex()];
      // fit_mean_err = EEmap_mean_err[eeid.hashedIndex()];
      // fit_sigma  = EEmap_sigma[eeid.hashedIndex()];
      // // fit_Snorm  = EEmap_Snorm[eeid.hashedIndex()];
      // // fit_b0     = EEmap_b0[eeid.hashedIndex()];
      // // fit_b1     = EEmap_b1[eeid.hashedIndex()];
      // // fit_b2     = EEmap_b2[eeid.hashedIndex()];
      // // fit_b3     = EEmap_b3[eeid.hashedIndex()];
      // // fit_Bnorm  = EEmap_Bnorm[eeid.hashedIndex()];

      if (EEmap_fitresptrToUse[eeid.hashedIndex()] >= 0 && EEmap_fitresptrToUse[eeid.hashedIndex()].Get() != nullptr) {
	Chisqu       = EEmap_fitresptrToUse[eeid.hashedIndex()]->Chi2();
	Ndof         = EEmap_fitresptrToUse[eeid.hashedIndex()]->Ndf();
	fit_mean     = EEmap_fitresptrToUse[eeid.hashedIndex()]->Parameter(1);  // for the double CB the mean is parameter [1]
	fit_mean_err = EEmap_fitresptrToUse[eeid.hashedIndex()]->ParError(1);
	fit_sigma    = EEmap_fitresptrToUse[eeid.hashedIndex()]->Parameter(2);
      } else {
	Chisqu       = -999;
	Ndof         = -999;
	fit_mean     = -999;
	fit_mean_err = -999;
	fit_sigma    = -999;
      }

      treeEE->Fill();

    }

  }

  treeEB->Write();
  treeEE->Write();

  outfile_->Write();
  outfile_->Close();
  cout << "FIT_EPSILON:  done" << endl;

}

//==============================================================

//==========================

void FitEpsilonPlot::saveCoefficientsEoverEtrueRooFit(const bool isSecondGenPhoton = false) 
{

  // important, if using the second photon the output file is updated, so the call with isSecondGenPhoton = true should be made as the second one
  // otherwise, based on the current implementation, at the time you open the file for the first photon the file would be overwritten due to RECREATE mode

  /// output file
  std::string fileName = outputDir_  + "/" + outfilename_;
  if (isSecondGenPhoton) outfile_ = new TFile(fileName.c_str(),"UPDATE");
  else                   outfile_ = new TFile(fileName.c_str(),"RECREATE");
  cout << "FIT_EPSILON: Saving E/Etrue Coefficients in " << fileName << " ... " << endl;;
  if(!outfile_) throw cms::Exception("WritingOutputFile") << "It was no possible to create output file " << fileName << "\n";
  outfile_->cd();

  // 2D calib map in the barrel
  TH2F* hmap_EB = new TH2F((isSecondGenPhoton ? "calibMap_EB_g2" : "calibMap_EB"),"EB calib coefficients: #eta on x, #phi on y",
			   2*EBDetId::MAX_IETA+1,-EBDetId::MAX_IETA-0.5,EBDetId::MAX_IETA+0.5,
			   EBDetId::MAX_IPHI, EBDetId::MIN_IPHI-0.5, EBDetId::MAX_IPHI+0.5 );
  hmap_EB->GetXaxis()->SetTitle("i#eta");
  hmap_EB->GetYaxis()->SetTitle("i#phi");
  TH2F* hmap_EEp = new TH2F((isSecondGenPhoton ? "calibMap_EEp_g2" : "calibMap_EEp"),"EE+ calib coefficients",100,0.5,100.5,100,0.5,100.5);
  hmap_EEp->GetXaxis()->SetTitle("ix");
  hmap_EEp->GetYaxis()->SetTitle("iy");
  TH2F* hmap_EEm = new TH2F((isSecondGenPhoton ? "calibMap_EEm_g2" : "calibMap_EEm"),"EE- calib coefficients",100,0.5,100.5,100,0.5,100.5);
  hmap_EEm->GetXaxis()->SetTitle("ix");
  hmap_EEm->GetYaxis()->SetTitle("iy");
  TH1F* hint = new TH1F("hint","Bin1: inRangeFit_ Bin2: finRangeFit_ Bin3: Barrel(0)/Endcap(1)",3,0.,3.);
  hint->SetBinContent(1,inRangeFit_);
  hint->SetBinContent(2,finRangeFit_);
  if( EEoEB_ == "Barrel" ) hint->SetBinContent(3,0);
  else                     hint->SetBinContent(3,1);
  hint->Write();

  EcalRegionalCalibrationBase* regCalibToUse = (isSecondGenPhoton) ? regionalCalibration_g2_ : regionalCalibration_;

  //filling Barrel Map
  for(int j=0; j<regCalibToUse->getCalibMap()->getNRegionsEB(); ++j)  
    {
      std::vector<DetId> ids = regCalibToUse->allDetIdsInEBRegion(j);
      for(std::vector<DetId>::const_iterator iid = ids.begin(); iid != ids.end(); ++iid) {
  	EBDetId ebid(*iid);
  	int ix = ebid.ieta()+EBDetId::MAX_IETA+1;

  	float coeffValue = regCalibToUse->getCalibMap()->coeff(*iid) > 0. ? regCalibToUse->getCalibMap()->coeff(*iid) : 1.;
  	hmap_EB->SetBinContent( ix, ebid.iphi(), coeffValue );
      } // loop over DetId in regions
    }

  hmap_EB->SetMinimum(0.9);
  hmap_EB->SetStats(false);
  hmap_EB->Write();

  for(int jR=0; jR < regCalibToUse->getCalibMap()->getNRegionsEE(); jR++)
    {
      std::vector<DetId> ids = regCalibToUse->allDetIdsInEERegion(jR);
      for(std::vector<DetId>::const_iterator iid = ids.begin(); iid != ids.end(); ++iid) 
  	{ 
  	  EEDetId eeid(*iid);
  	  float coeffValue =  regCalibToUse->getCalibMap()->coeff(*iid) > 0. ?  regCalibToUse->getCalibMap()->coeff(*iid) : 1.;

  	  if(eeid.positiveZ())
  	    hmap_EEp->Fill(eeid.ix(), eeid.iy(), coeffValue); 
  	  else 
  	    hmap_EEm->Fill(eeid.ix(), eeid.iy(), coeffValue);
  	}
    }

  hmap_EEp->SetMinimum(0.9);
  hmap_EEp->SetStats(false);
  hmap_EEp->Write();

  hmap_EEm->SetMinimum(0.9);
  hmap_EEm->SetStats(false);
  hmap_EEm->Write();

  /*------------- TTREE --------------*/
  uint32_t   rawId;
  int        hashedIndex;
  int        ieta;
  int        iphi;
  int        iSM;
  int        iMod;
  int        iTT;
  int        iTTeta;
  int        iTTphi;
  int        iter = currentIteration_;
  float      regCoeff;
  float      Signal;//#
  float      Backgr; 
  float      Chisqu; 
  float      Ndof; 
  float      fit_mean;
  float      fit_mean_err;
  float      fit_sigma;
  float      fit_Snorm;
  float      fit_b0;
  float      fit_b1;    
  float      fit_b2;    
  float      fit_b3;    
  float      fit_Bnorm; 
  /// endcap variables
  int ix;
  int iy;
  int zside;
  int sc; 
  int isc;
  int ic;
  int iquadrant;

  TTree* treeEB = new TTree((isSecondGenPhoton ? "calibEB_g2" : "calibEB"),"Tree of EB Inter-calibration constants");
  TTree* treeEE = new TTree((isSecondGenPhoton ? "calibEE_g2" : "calibEE"),"Tree of EE Inter-calibration constants");

  /// barrel
  treeEB->Branch("rawId",&rawId,"rawId/i");
  treeEB->Branch("hashedIndex",&hashedIndex,"hashedIndex/I");
  treeEB->Branch("ieta",&ieta,"ieta/I");
  treeEB->Branch("iphi",&iphi,"iphi/I");
  treeEB->Branch("iSM",&iSM,"iSM/I");
  treeEB->Branch("iMod",&iMod,"iMod/I");
  treeEB->Branch("iTT",&iTT,"iTT/I");
  treeEB->Branch("iTTeta",&iTTeta,"iTTeta/I");
  treeEB->Branch("iTTphi",&iTTphi,"iTTphi/I");
  treeEB->Branch("iter",&iter,"iter/I");
  treeEB->Branch("coeff",&regCoeff,"coeff/F");
  treeEB->Branch("Signal",&Signal,"Signal/F");//#
  treeEB->Branch("Backgr",&Backgr,"Backgr/F");
  treeEB->Branch("Chisqu",&Chisqu,"Chisqu/F");
  treeEB->Branch("Ndof",&Ndof,"Ndof/F");
  treeEB->Branch("fit_mean",&fit_mean,"fit_mean/F");
  treeEB->Branch("fit_mean_err",&fit_mean_err,"fit_mean_err/F");
  treeEB->Branch("fit_sigma",&fit_sigma,"fit_sigma/F");
  treeEB->Branch("fit_Snorm",&fit_Snorm,"fit_Snorm/F");
  treeEB->Branch("fit_b0",&fit_b0,"fit_b0/F");
  treeEB->Branch("fit_b1",&fit_b1,"fit_b1/F");
  treeEB->Branch("fit_b2",&fit_b2,"fit_b2/F");
  treeEB->Branch("fit_b3",&fit_b3,"fit_b3/F");
  treeEB->Branch("fit_Bnorm",&fit_Bnorm,"fit_Bnorm/F");

  /// endcap
  treeEE->Branch("ix",&ix,"ix/I");
  treeEE->Branch("iy",&iy,"iy/I");
  treeEE->Branch("zside",&zside,"zside/I");
  treeEE->Branch("sc",&sc,"sc/I");
  treeEE->Branch("isc",&isc,"isc/I");
  treeEE->Branch("ic",&ic,"ic/I");
  treeEE->Branch("iquadrant",&iquadrant,"iquadrant/I");
  treeEE->Branch("hashedIndex",&hashedIndex,"hashedIndex/I");
  treeEE->Branch("iter",&iter,"iter/I");
  treeEE->Branch("coeff",&regCoeff,"coeff/F");
  treeEE->Branch("Signal",&Signal,"Signal/F");//#
  treeEE->Branch("Backgr",&Backgr,"Backgr/F");
  treeEE->Branch("Chisqu",&Chisqu,"Chisqu/F");
  treeEE->Branch("Ndof",&Ndof,"Ndof/F");
  treeEE->Branch("fit_mean",&fit_mean,"fit_mean/F");
  treeEE->Branch("fit_mean_err",&fit_mean_err,"fit_mean_err/F");
  treeEE->Branch("fit_sigma",&fit_sigma,"fit_sigma/F");
  treeEE->Branch("fit_Snorm",&fit_Snorm,"fit_Snorm/F");
  treeEE->Branch("fit_b0",&fit_b0,"fit_b0/F");
  treeEE->Branch("fit_b1",&fit_b1,"fit_b1/F");
  treeEE->Branch("fit_b2",&fit_b2,"fit_b2/F");
  treeEE->Branch("fit_b3",&fit_b3,"fit_b3/F");
  treeEE->Branch("fit_Bnorm",&fit_Bnorm,"fit_Bnorm/F");

  for(int iR=0; iR < regCalibToUse->getCalibMap()->getNRegionsEB(); ++iR)  {

    std::vector<DetId> ids = regCalibToUse->allDetIdsInEBRegion(iR);
    for(std::vector<DetId>::const_iterator iid = ids.begin(); iid != ids.end(); ++iid) {

      EBDetId ebid(*iid);
      hashedIndex = ebid.hashedIndex();
      ieta = ebid.ieta();
      iphi = ebid.iphi();
      iSM = ebid.ism();
      iMod = ebid.im();
      iTT  = ebid.tower().hashedIndex();
      iTTeta = ebid.tower_ieta();
      iTTphi = ebid.tower_iphi();
      if (isSecondGenPhoton) {
	Signal = EBmap_Signal_g2[ebid.hashedIndex()];//#
	Backgr = EBmap_Backgr_g2[ebid.hashedIndex()];
	Chisqu = EBmap_Chisqu_g2[ebid.hashedIndex()];
	Ndof = EBmap_ndof_g2[ebid.hashedIndex()];
	fit_mean     = EBmap_mean_g2[ebid.hashedIndex()];
	fit_mean_err = EBmap_mean_err_g2[ebid.hashedIndex()];
	fit_sigma  = EBmap_sigma_g2[ebid.hashedIndex()];
	fit_Snorm  = EBmap_Snorm_g2[ebid.hashedIndex()];
	fit_b0     = EBmap_b0_g2[ebid.hashedIndex()];
	fit_b1     = EBmap_b1_g2[ebid.hashedIndex()];
	fit_b2     = EBmap_b2_g2[ebid.hashedIndex()];
	fit_b3     = EBmap_b3_g2[ebid.hashedIndex()];
	fit_Bnorm  = EBmap_Bnorm_g2[ebid.hashedIndex()];
      } else {
	Signal = EBmap_Signal[ebid.hashedIndex()];//#
	Backgr = EBmap_Backgr[ebid.hashedIndex()];
	Chisqu = EBmap_Chisqu[ebid.hashedIndex()];
	Ndof = EBmap_ndof[ebid.hashedIndex()];
	fit_mean     = EBmap_mean[ebid.hashedIndex()];
	fit_mean_err = EBmap_mean_err[ebid.hashedIndex()];
	fit_sigma  = EBmap_sigma[ebid.hashedIndex()];
	fit_Snorm  = EBmap_Snorm[ebid.hashedIndex()];
	fit_b0     = EBmap_b0[ebid.hashedIndex()];
	fit_b1     = EBmap_b1[ebid.hashedIndex()];
	fit_b2     = EBmap_b2[ebid.hashedIndex()];
	fit_b3     = EBmap_b3[ebid.hashedIndex()];
	fit_Bnorm  = EBmap_Bnorm[ebid.hashedIndex()];
      }

      regCoeff = regCalibToUse->getCalibMap()->coeff(*iid);

      treeEB->Fill();
    } // loop over DetId in regions
  } // loop over regions

  for(int jR=0; jR < regCalibToUse->getCalibMap()->getNRegionsEE() ; jR++) {

    std::vector<DetId> ids = regCalibToUse->allDetIdsInEERegion(jR);

    for(std::vector<DetId>::const_iterator iid = ids.begin(); iid != ids.end(); ++iid) { 

      EEDetId eeid(*iid);
      ix = eeid.ix();
      iy = eeid.iy();
      zside = eeid.zside();
      sc = eeid.sc();
      isc = eeid.isc();
      ic = eeid.ic();
      iquadrant = eeid.iquadrant();
      hashedIndex = eeid.hashedIndex();
      regCoeff = regCalibToUse->getCalibMap()->coeff(*iid);
      if (isSecondGenPhoton) {
	Signal = EEmap_Signal_g2[eeid.hashedIndex()];//#
	Backgr = EEmap_Backgr_g2[eeid.hashedIndex()];
	Chisqu = EEmap_Chisqu_g2[eeid.hashedIndex()];            
	Ndof = EEmap_ndof_g2[eeid.hashedIndex()];            
	fit_mean     = EEmap_mean_g2[eeid.hashedIndex()];
	fit_mean_err = EEmap_mean_err_g2[eeid.hashedIndex()];
	fit_sigma  = EEmap_sigma_g2[eeid.hashedIndex()];
	fit_Snorm  = EEmap_Snorm_g2[eeid.hashedIndex()];
	fit_b0     = EEmap_b0_g2[eeid.hashedIndex()];
	fit_b1     = EEmap_b1_g2[eeid.hashedIndex()];
	fit_b2     = EEmap_b2_g2[eeid.hashedIndex()];
	fit_b3     = EEmap_b3_g2[eeid.hashedIndex()];
	fit_Bnorm  = EEmap_Bnorm_g2[eeid.hashedIndex()];
      }

      treeEE->Fill();

    }

  }

  treeEB->Write();
  treeEE->Write();

  outfile_->Write();
  outfile_->Close();
  cout << "FIT_EPSILON:  done" << endl;

}




// ------------ method called for each event  ------------

void FitEpsilonPlot::analyze(const edm::Event& iEvent, const edm::EventSetup& iSetup)
{

    TF1 ffit("gausa","gaus(0)+[3]*x+[4]",-0.5,0.5);
    ffit.SetParameters(100,0,0.1);
    ffit.SetParNames("Constant","Mean_value","Sigma","a","b");

    ffit.SetParLimits(3,-500,500);
    ffit.SetParLimits(2,0.05,0.22);

    cout << "FIT_EPSILON: About to fit epsilon distributions" << endl; 

    /// compute average weight, eps, and update calib constant
    if( (EEoEB_ == "Barrel") && (Barrel_orEndcap_=="ONLY_BARREL" || Barrel_orEndcap_=="ALL_PLEASE" ) ){

      for(uint32_t j= (uint32_t)inRangeFit_; j <= (uint32_t)finRangeFit_ && j < (uint32_t)regionalCalibration_->getCalibMap()->getNRegionsEB(); ++j)  
	{
	  cout<<"FIT_EPSILON: Fitting EB Cristal--> "<<j<<endl;

	  if(!(j%1000)) cout << "FIT_EPSILON: fitting EB region " << j << endl;

	  float mean = 0.;
	  float mean_g2 = 0.; // used only for E/Etrue with MC	      

	  if (isEoverEtrue_) {
		  
	    int crystalIndexInSM = getArrayIndexOfFoldedSMfromDenseIndex(j);
	    TH1F* histoToFit_g1 = (foldInSuperModule_ ? EoverEtrue_g1_EB_SM_hvec[crystalIndexInSM] : EoverEtrue_g1_EB_h[j]);
		  
	    // first photon 
	    // int iMin = EoverEtrue_g1_EB_h[j]->GetXaxis()->FindFixBin(0.6); 
	    // int iMax = EoverEtrue_g1_EB_h[j]->GetXaxis()->FindFixBin(1.1);
	    // double integral = EoverEtrue_g1_EB_h[j]->Integral(iMin, iMax);  
	    double integral = histoToFit_g1->Integral();  

	    if(integral > EoverEtrue_integralMin) {

	      if (fitEoverEtrueWithRooFit_) {
		Pi0FitResult fitres = FitEoverEtruePeakRooFit(histoToFit_g1, false, j, Pi0EB);
		RooRealVar* mean_fitresult = (RooRealVar*)(((fitres.res)->floatParsFinal()).find("mean"));
		mean = mean_fitresult->getVal();
	      } else {
		TFitResultPtr fitresptr = FitEoverEtruePeak(histoToFit_g1, false, j, Pi0EB, false);
		mean = fitresptr->Parameter(1);
		if (mean >= 1.5) mean = 0.; 		
	      }

	    } else {

	      std::cout << "### g1 ### FIT_EPSILON: iR = " << j << ", integral() = " << integral << " , skipping the fit " << std::endl;
	      mean = 0.;
	      if (not fitEoverEtrueWithRooFit_) {
		EBmap_fitresptr_g1[j] = TFitResultPtr(-1); 
	      }

	    }

	    TH1F* histoToFit_g2 = (foldInSuperModule_ ? EoverEtrue_g2_EB_SM_hvec[crystalIndexInSM] : EoverEtrue_g2_EB_h[j]);

	    // second photon 
	    // iMin = EoverEtrue_g2_EB_h[j]->GetXaxis()->FindFixBin(0.6); 
	    // iMax = EoverEtrue_g2_EB_h[j]->GetXaxis()->FindFixBin(1.1);
	    // integral = EoverEtrue_g2_EB_h[j]->Integral(iMin, iMax);  
	    integral = histoToFit_g2->Integral();  

	    if(integral > EoverEtrue_integralMin) {

	      if (fitEoverEtrueWithRooFit_) {
		Pi0FitResult fitres = FitEoverEtruePeakRooFit(histoToFit_g2, true, j, Pi0EB);
		RooRealVar* mean_fitresult = (RooRealVar*)(((fitres.res)->floatParsFinal()).find("mean"));
		mean_g2 = mean_fitresult->getVal();
	      } else {
		TFitResultPtr fitresptr = FitEoverEtruePeak(histoToFit_g2, true, j, Pi0EB, false);
		mean_g2 = fitresptr->Parameter(1);
		if (mean_g2 >= 1.5) mean_g2 = 0.; 
	      }
		    
	    } else {

	      std::cout << "### g2 ### FIT_EPSILON: iR = " << j << ", integral() = " << integral << " , skipping the fit " << std::endl;
	      mean_g2 = 0.;
	      if (not fitEoverEtrueWithRooFit_) {
		EBmap_fitresptr_g2[j] = TFitResultPtr(-1);
	      }

	    }
		  
	  } else {
		  
	    if(!useMassInsteadOfEpsilon_ && epsilon_EB_h[j]->Integral(epsilon_EB_h[j]->GetNbinsX()*(1./6.),epsilon_EB_h[j]->GetNbinsX()*0.5) > 20) 
	      {

		double Max = 0.;
		double Min = -0.5, bin = 0.0125;
		Max = Min+(bin*(double)epsilon_EB_h[j]->GetMaximumBin());
		double Bound1 = -0.15, Bound2 = 0.25;
		if ( fabs(Max+Bound1) > 0.24  ){ Bound1 = -0.1;}
		if ( Max+Bound2 > 0.34  ){ Bound2 = 0.15;}
		if ( fabs(Max+Bound1) > 0.24  ){ Bound1 = -0.075;}
		if ( Max+Bound2 > 0.34  ){ Bound2 = 0.1;}
		if ( fabs(Max+Bound1) > 0.24  ){ Bound1 = -0.03;}
		if ( Max+Bound2 > 0.34  ){ Bound2 = 0.05;}
		if ( fabs(Max+Bound1) > 0.24  ){ Bound1 = -0.009;}
		if ( Max+Bound2 > 0.34  ){ Bound2 = 0.01;}

		epsilon_EB_h[j]->Fit(&ffit,"qB","", Max+Bound1,Max+Bound2);
		if(ffit.GetNDF() != 0) {
		  double chi2 = ( ffit.GetChisquare()/ffit.GetNDF() );

		  if ( chi2  > 11 ){
		    ffit.SetParLimits(2,0.05,0.15);
		    ffit.SetParameters(100,0,0.1);
		    epsilon_EB_h[j]->Fit(&ffit,"qB","", Max+Bound1,Max+Bound2);
		    chi2 = (ffit.GetChisquare()/ffit.GetNDF());
		    if ( chi2  < 11 ){   cout<<"Saved 1 Level!!"<<endl;  }
		    else{
		      ffit.SetParameters(100,0,0.1);
		      ffit.SetParLimits(2,0.05,0.1);
		      epsilon_EB_h[j]->Fit(&ffit,"qB","",  Max+Bound1,Max+Bound2);
		      chi2 = (ffit.GetChisquare()/ffit.GetNDF());
		      if ( chi2  < 11 ){ cout<<"Saved 2 Level!!"<<endl; }
		      else{ cout<<"DAMN: High Chi square..."<<endl; }
		    }
		  }
		}
		else cout<<"DAMN: NDF == 0"<<endl;
		mean = ffit.GetParameter(1);
	      }
	    else if(useMassInsteadOfEpsilon_)
	      {

		int crystalIndexInSM = getArrayIndexOfFoldedSMfromDenseIndex(j);
		TH1F* histoToFit = (foldInSuperModule_ ? epsilon_EB_SM_hvec[crystalIndexInSM] : epsilon_EB_h[j]);

		int iMin = histoToFit->GetXaxis()->FindFixBin(Are_pi0_? 0.08:0.4 ); 
		int iMax = histoToFit->GetXaxis()->FindFixBin(Are_pi0_? 0.18:0.65 );
		double integral = histoToFit->Integral(iMin, iMax);  

		if(integral>60.) {

		  Pi0FitResult fitres = FitMassPeakRooFit( histoToFit, 
							   Are_pi0_? fitRange_low_pi0:fitRange_low_eta, 
							   Are_pi0_? fitRange_high_pi0:fitRange_high_eta, 
							   j, 1, Pi0EB, 0, isNot_2010_); //0.05-0.3
		  RooRealVar* mean_fitresult = (RooRealVar*)(((fitres.res)->floatParsFinal()).find("mean"));
		  mean = mean_fitresult->getVal();

		  float r2 = mean/(Are_pi0_? PI0MASS:ETAMASS);
		  r2 = r2*r2;
		  //cout<<"EBMEAN::"<<j<<":"<<mean<<" Saved if: "<<fitres.SoB<<">(isNot_2010_ ? 0.04:0.1) "<<(fitres.chi2/fitres.dof)<<" < 0.2 "<<fabs(mean-0.15)<<" >0.0000001) "<<endl;
		  //if( fitres.SoB>(isNot_2010_ ? 0.04:0.1) && (fitres.chi2/fitres.dof)< 0.5 && fabs(mean-0.15)>0.0000001) mean = 0.5 * ( r2 - 1. );
		  //if( fitres.chi2 < 5 && fabs(mean-(Are_pi0_? upper_bound_pi0mass_EB:upper_bound_etamass_EB))>0.0000001) mean = 0.5 * ( r2 - 1. );
		  if( fabs(mean-(Are_pi0_? upper_bound_pi0mass_EB:upper_bound_etamass_EB)) > 0.0000001 )
		    mean = 0.5 * ( r2 - 1. );
		  else 
		    mean = 0.;
		}
		else{
		  mean = 0.;
		}
	      }

	  }

	  std::vector<DetId> ids = regionalCalibration_->allDetIdsInEBRegion(j);
	  // actually it is just one crystal, unless we do a calibration based on trigger towers or etaring
	  for(std::vector<DetId>::const_iterator iid = ids.begin(); iid != ids.end(); ++iid) 
	    {
	      if (isEoverEtrue_) regionalCalibration_->getCalibMap()->coeff(*iid) *= (mean==0.) ? 1. : 1./(mean);
	      else               regionalCalibration_->getCalibMap()->coeff(*iid) *= (mean==0.) ? 1. : 1./(1.+mean);
	    } // loop over DetId in regions

	  if (isEoverEtrue_) {
	    ids = regionalCalibration_g2_->allDetIdsInEBRegion(j);
	    for(std::vector<DetId>::const_iterator iid = ids.begin(); iid != ids.end(); ++iid) 
	      {
		regionalCalibration_g2_->getCalibMap()->coeff(*iid) *= (mean_g2==0.) ? 1. : 1./(mean_g2);
	      } // loop over DetId in regions
	  }
		  
	} // loop over regions

    }// if you have to fit barrel

    /// loop over EE crystals
    if( (EEoEB_ == "Endcap") && (Barrel_orEndcap_=="ONLY_ENDCAP" || Barrel_orEndcap_=="ALL_PLEASE" ) ){

      for(int jR = inRangeFit_; jR <=finRangeFit_ && jR < regionalCalibration_->getCalibMap()->getNRegionsEE(); jR++)
	{
	  cout << "FIT_EPSILON: Fitting EE Cristal--> " << jR << endl;
	  if(!(jR%1000))
	    cout << "FIT_EPSILON: fitting EE region " << jR << endl;

	  float mean = 0.;
	  float mean_g2 = 0.; // used only for E/Etrue with MC

	  if (isEoverEtrue_) {
		  
	    // int iMin = EoverEtrue_g1_EE_h[jR]->GetXaxis()->FindFixBin(0.6); 
	    // int iMax = EoverEtrue_g1_EE_h[jR]->GetXaxis()->FindFixBin(1.1);
	    // double integral = EoverEtrue_g1_EE_h[jR]->Integral(iMin, iMax);  
	    double integral = EoverEtrue_g1_EE_h[jR]->Integral();  

	    if(integral > EoverEtrue_integralMin) {

	      Pi0FitResult fitres = FitEoverEtruePeakRooFit(EoverEtrue_g1_EE_h[jR], false, jR, Pi0EE);
	      RooRealVar* mean_fitresult = (RooRealVar*)(((fitres.res)->floatParsFinal()).find("mean"));
	      mean = mean_fitresult->getVal();
		    
	    } else {

	      std::cout << "### g1 ### FIT_EPSILON: iR = " << jR << ", integral() = " << integral << " , skipping the fit " << std::endl;
	      mean = 0.;

	    }

	    // if(integral > EoverEtrue_integralMin) {

	    //   TFitResultPtr fitresptr = FitEoverEtruePeak( EoverEtrue_g1_EE_h[jR], false, jR, Pi0EE, false);
	    //   mean = fitresptr->Parameter(1);
	    //   if (mean >= 1.5) mean = 0.; 
		    
	    // } else {

	    //   std::cout << "### g1 ### FIT_EPSILON: iR = " << jR << ", integral() = " << integral << " , skipping the fit " << std::endl;
	    //   mean = 0.;
	    //   EEmap_fitresptr_g1[jR] = TFitResultPtr(-1);

	    // }

	    // iMin = EoverEtrue_g2_EE_h[jR]->GetXaxis()->FindFixBin(0.6); 
	    // iMax = EoverEtrue_g2_EE_h[jR]->GetXaxis()->FindFixBin(1.1);
	    // integral = EoverEtrue_g2_EE_h[jR]->Integral(iMin, iMax);  
	    integral = EoverEtrue_g2_EE_h[jR]->Integral();  

	    if(integral > EoverEtrue_integralMin) {

	      Pi0FitResult fitres = FitEoverEtruePeakRooFit(EoverEtrue_g1_EE_h[jR], true, jR, Pi0EE);
	      RooRealVar* mean_fitresult = (RooRealVar*)(((fitres.res)->floatParsFinal()).find("mean"));
	      mean_g2 = mean_fitresult->getVal();
		    
	    } else {

	      std::cout << "### g2 ### FIT_EPSILON: iR = " << jR << ", integral() = " << integral << " , skipping the fit " << std::endl;
	      mean_g2 = 0.;

	    }

	    // if(integral > EoverEtrue_integralMin) {

	    //   TFitResultPtr fitresptr = FitEoverEtruePeak( EoverEtrue_g2_EE_h[jR], true, jR, Pi0EE, false);
	    //   mean_g2 = fitresptr->Parameter(1);
	    //   if (mean_g2 >= 1.5) mean_g2 = 0.; 
		    
	    // } else {

	    //   std::cout << "### g2 ### FIT_EPSILON: iR = " << jR << ", integral() = " << integral << " , skipping the fit " << std::endl;
	    //   mean_g2 = 0.;
	    //   EEmap_fitresptr_g2[jR] = TFitResultPtr(-1);

	    // }
		  
	  } else {
		
	    if(!useMassInsteadOfEpsilon_ && epsilon_EE_h[jR]->Integral(epsilon_EE_h[jR]->GetNbinsX()*(1./6.),epsilon_EE_h[jR]->GetNbinsX()*0.5) > 20) 
	      {
		TF1 *ffit = new TF1("gausa","gaus(0)+[3]*x+[4]",-0.5,0.5);
		ffit->SetParameters(100,0,0.1);
		ffit->SetParNames("Constant","Mean_value","Sigma","a","b");

		ffit->SetParLimits(0,0.,epsilon_EE_h[jR]->GetEntries()*1.1);
		ffit->SetParLimits(3,-500,500);
		ffit->SetParLimits(2,0.05,0.3);

		double Max = 0.;
		double Min = -0.5, bin = 0.0125;
		Max = Min+(bin*(double)epsilon_EE_h[jR]->GetMaximumBin());
		double Bound1 = -0.35, Bound2 = 0.35;
		if ( fabs(Max+Bound1) > 0.38  ){ Bound1 = -0.3;}
		if ( Max+Bound2 > 0.48  ){ Bound2 = 0.3;}
		if ( fabs(Max+Bound1) > 0.38  ){ Bound1 = -0.25;}
		if ( Max+Bound2 > 0.48  ){ Bound2 = 0.2;}
		if ( fabs(Max+Bound1) > 0.38  ){ Bound1 = -0.2;}
		if ( Max+Bound2 > 0.48  ){ Bound2 = 0.15;}
		if ( fabs(Max+Bound1) > 0.38  ){ Bound1 = -0.15;}
		if ( Max+Bound2 > 0.48  ){ Bound2 = 0.1;}
		if ( fabs(Max+Bound1) > 0.38  ){ Bound1 = -0.1;}
		if ( fabs(Max+Bound1) > 0.38  ){ Bound1 = -0.05;}
		//@@IterativeFit(epsilon_EE_h[jR], *ffit);
		//@@mean = ffit.GetParameter(1); 
		epsilon_EE_h[jR]->Fit(ffit,"qB","", Max+Bound1,Max+Bound2);

		if(ffit->GetNDF() != 0) {
		  double chi2 = ( ffit->GetChisquare()/ffit->GetNDF() );
		  if(chi2 > 11  ) { cout<<"DAMN:(EE) High Chi square..."<<endl; }
		}
		else cout<<"DAMN: NDF == 0"<<endl;
		mean = ffit->GetParameter(1);
	      }
	    else if(useMassInsteadOfEpsilon_)
	      {
		int iMin = epsilon_EE_h[jR]->GetXaxis()->FindFixBin(Are_pi0_? 0.08:0.4 ); 
		int iMax = epsilon_EE_h[jR]->GetXaxis()->FindFixBin(Are_pi0_? 0.18:0.65 );
		double integral = epsilon_EE_h[jR]->Integral(iMin, iMax);  

		if(integral>70.)
		  {
		    Pi0FitResult fitres = FitMassPeakRooFit( epsilon_EE_h[jR], 
							     Are_pi0_? fitRange_low_pi0:fitRange_low_eta, 
							     Are_pi0_? fitRange_high_pi0:fitRange_high_eta, 
							     jR, 1, Pi0EE, 0, isNot_2010_);//0.05-0.3
		    RooRealVar* mean_fitresult = (RooRealVar*)(((fitres.res)->floatParsFinal()).find("mean"));
		    mean = mean_fitresult->getVal();
		    float r2 = mean/(Are_pi0_? PI0MASS:ETAMASS);
		    r2 = r2*r2;
		    //cout<<"EEMEAN::"<<jR<<":"<<mean<<" Saved if: "<<fitres.SoB<<">0.3 "<<(fitres.chi2/fitres.dof)<<" < (isNot_2010_? 0.07:0.35) "<<fabs(mean-0.14)<<" >0.0000001) "<<endl;
		    //if( (fitres.chi2/fitres.dof)<0.3 && fitres.SoB>(isNot_2010_? 0.07:0.35) && fabs(mean-0.14)>0.0000001 ) mean = 0.5 * ( r2 - 1. );
		    //if( fitres.chi2 < 5 && fabs(mean-(Are_pi0_? upper_bound_pi0mass_EE:upper_bound_etamass_EE))>0.0000001 ) mean = 0.5 * ( r2 - 1. );
		    // do not use Chi2 for goodness of fit. If I have many events, then the chi2 will be huge because the model will not pass through all data points
		    // on the oter hand, if I have few events, the statistical uncertainty is large and the Chi2 tends to be little
		    // better not to use Chi2
		    if(fabs(mean-(Are_pi0_? upper_bound_pi0mass_EE:upper_bound_etamass_EE))>0.0000001 ) 
		      mean = 0.5 * ( r2 - 1. );
		    else
		      mean = 0.;
		  }
		else
		  {
		    mean = 0.; 
		  }
	      }

	  }

	  std::vector<DetId> ids = regionalCalibration_->allDetIdsInEERegion(jR);
	  for(std::vector<DetId>::const_iterator iid = ids.begin(); iid != ids.end(); ++iid) 
	    {
	      if (isEoverEtrue_) regionalCalibration_->getCalibMap()->coeff(*iid) *= (mean==0.) ? 1. : 1./(mean);
	      else               regionalCalibration_->getCalibMap()->coeff(*iid) *= (mean==0.) ? 1. : 1./(1.+mean);
	    }

	  // now loop on second photon if doing E/Etrue
	  if (isEoverEtrue_) {
	    ids = regionalCalibration_g2_->allDetIdsInEERegion(jR);
	    for(std::vector<DetId>::const_iterator iid = ids.begin(); iid != ids.end(); ++iid) 
	      {
		regionalCalibration_g2_->getCalibMap()->coeff(*iid) *= (mean_g2==0.) ? 1. : 1./(mean_g2);
	      } // loop over DetId in regions		  
	  }

	}//for EE

    }// if you have to fit Endcap

}



void FitEpsilonPlot::IterativeFit(TH1F* h, TF1 & ffit) 
{
    float chi2 = 1000.;
    const int iterMax = 10;

    h->Fit(&ffit,"q","",-0.4,0.4);

    float mean = (ffit.GetParameters())[1];
    float sigma = (ffit.GetParameters())[2];
    float xmin = mean-2.*sigma;
    float xmax = mean+2.*sigma;

    ffit.SetRange(xmin,xmax);

    double par[3] = { ffit.GetParameters()[0], mean, sigma };

    for(int iter=0; iter< iterMax && chi2>5.; ++iter) 
    {
	  ffit.SetParameters(par[0],par[1], par[2]);

	  h->Fit(&ffit,"q","",xmin,xmax);
	  par[0] = (ffit.GetParameters())[0];
	  par[1] = (ffit.GetParameters())[1];
	  par[2] = (ffit.GetParameters())[2];

	  if(ffit.GetNDF()!=0) {
		chi2 = ffit.GetChisquare()/ffit.GetNDF();
	  }

    }
    return;
}


//-----------------------------------------------------------------------------------

Pi0FitResult FitEpsilonPlot::FitMassPeakRooFit(TH1F* h, double xlo, double xhi,  uint32_t HistoIndex, int ngaus, FitMode mode, int niter, bool isNot_2010_) 
{
    //-----------------------------------------------------------------------------------

    std::stringstream ind;
    ind << (int) HistoIndex;
    TString nameHistofit = "Fit_n_" + ind.str() + Form("_attempt%d",niter);

    // add canvas to save rooplot on top (will save this in the file)
    TCanvas* canvas = new TCanvas((nameHistofit+Form("_c")).Data(),"",700,700);
    canvas->cd();
    canvas->SetTickx(1);
    canvas->SetTicky(1);
    canvas->cd();
    canvas->SetRightMargin(0.06);
    canvas->SetLeftMargin(0.15);

    Double_t upMassBoundaryEB = Are_pi0_? upper_bound_pi0mass_EB:upper_bound_etamass_EB; 
    Double_t upMassBoundaryEE = Are_pi0_? upper_bound_pi0mass_EB:upper_bound_etamass_EE; 
    Double_t upMassBoundary = (mode==Pi0EB) ? upMassBoundaryEB : upMassBoundaryEE;
    // need a patch for some crystals in EB that might have the peak around 160 MeV due to high laser corrections.
    // depending on the year, the containment corrections might also increase a bit the peak position
    // the problem is that the peak is expected to be below 150 MeV when defining the signal model, so we have to catch these exception
    Double_t xValMaxHisto = h->GetXaxis()->GetBinCenter(h->GetMaximumBin()+1); // use value just above maximum, it will be used to set the mean of the gaussian
    // check the maximum is within xlo and xhi (the histogram range is larger than the fit range)
    Double_t maxMassForGaussianMean = 0.0; //upper_bound_pi0mass_EB;
    // first check if peak is in the fit range (for EB it will be, in EE the background rises up and the maximum might not coincide wth peak)
    if (xValMaxHisto < xhi) {

      if (xValMaxHisto > upMassBoundary) {
	maxMassForGaussianMean = xValMaxHisto;
	xhi = Are_pi0_? fitRange_high_pi0_ext : fitRange_high_eta_ext;      //xhi + 0.012; // increase a bit the fit range
      } else {
	maxMassForGaussianMean = upMassBoundary;
      }

    } else {

      // need to loop on bins in the fit window
      Double_t ymaxHisto = 0.0;
      Int_t binYmaxHisto = -1;
      for (Int_t ibin = h->GetXaxis()->FindFixBin(xlo); ibin <= h->GetXaxis()->FindFixBin(xhi); ibin++) {
	if (h->GetBinContent(ibin) > ymaxHisto) {
	  ymaxHisto = h->GetBinContent(ibin);
	  binYmaxHisto = ibin;
	}
      }
      // check if maximum was found and it was not the last-1 bin
      // in that case, use the next bin to get max value for mass, just to avoid biases (that's why we asked last-1)
      if (binYmaxHisto > 0 && binYmaxHisto < (h->GetXaxis()->FindFixBin(xhi)-1)) {
	maxMassForGaussianMean = h->GetXaxis()->GetBinCenter(binYmaxHisto+1);
	if (maxMassForGaussianMean > upMassBoundary) xhi = Are_pi0_? fitRange_high_pi0_ext : fitRange_high_eta_ext;  //xhi + 0.012; // increase a bit the fit range

      } else {
	maxMassForGaussianMean = upMassBoundary; // if all this mess didn't work, just use the value we would have used in the beginning
      }

    }
    
    RooRealVar x("x","#gamma#gamma invariant mass",xlo, xhi, "GeV/c^2");

    RooDataHist dh("dh","#gamma#gamma invariant mass",RooArgList(x),h);

    //RooRealVar mean("mean","#pi^{0} peak position", Are_pi0_? 0.13:0.52,  Are_pi0_? 0.105:0.5, Are_pi0_? upper_bound_pi0mass_EB:upper_bound_etamass_EB,"GeV/c^{2}");
    RooRealVar mean("mean","#pi^{0} peak position", Are_pi0_? 0.13:0.52,  Are_pi0_? 0.105:0.5, maxMassForGaussianMean,"GeV/c^{2}");
    RooRealVar sigma("sigma","#pi^{0} core #sigma",0.011, 0.005,0.015,"GeV/c^{2}");


    if(mode==Pi0EE)  {
	  mean.setRange( Are_pi0_? 0.1:0.45, maxMassForGaussianMean);
	  mean.setVal(Are_pi0_? 0.13:0.55);
	  sigma.setRange(0.005, 0.020);
    }
    if(mode==Pi0EB && niter==1){
	  mean.setRange(Are_pi0_? 0.105:0.47, maxMassForGaussianMean);
	  sigma.setRange(0.003, 0.030);
    }

    //RooRealVar Nsig("Nsig","#pi^{0} yield",1000.,0.,1.e7);
    RooRealVar Nsig("Nsig","#pi^{0} yield",h->Integral()*0.15,0.,h->Integral()*10.0);
    //Nsig.setVal( h->GetSum()*0.1);

    RooGaussian gaus("gaus","Core Gaussian",x, mean,sigma);

    RooRealVar sigmaTail("sigmaTail","#pi^{0} tail #sigma",0.040, 0.020,0.065,"GeV/c^{2}");

    RooGaussian gaus2("gaus2","Tail Gaussian",x, mean,sigmaTail);

    RooRealVar fcore("fcore","f_{core}",0.9,0.,1.);
    RooAddPdf  signal("signal","signal model",RooArgList(gaus,gaus2),fcore);

    RooRealVar p0("p0","p0", 1000.,-1.e5,1.e5);
    RooRealVar p1("p1","p1", -3000.,-1.e5,1.e5);
    RooRealVar p2("p2","p2", 10000.,-1.e5,1.e5);
    RooRealVar p3("p3","p3", -10000.,-1.e5,1.e5);
    RooRealVar p4("p4","p4",-4000.,-1.e5,1.e5);
    RooRealVar p5("p5","p5", 5.,-1.e5,1.e5);
    RooRealVar p6("p6","p6", 6.,-1.e5,1.e5);

    RooRealVar cb0("cb0","cb0", 0.2, -1.,1.);
    RooRealVar cb1("cb1","cb1",-0.1, -1.,1.);
    RooRealVar cb2("cb2","cb2", 0.1,  -1.,1.);
    RooRealVar cb3("cb3","cb3",-0.1, -0.5,0.5);
    RooRealVar cb4("cb4","cb4", 0.1, -1.,1.);
    RooRealVar cb5("cb5","cb5", 0.1, -1.,1.);
    RooRealVar cb6("cb6","cb6", 0.3, -1.,1.);


    //RooChebychev bkg("bkg","bkg model", x, RooArgList(cb0,cb1,cb2) );
    //RooChebychev bkg("bkg","bkg model", x, RooArgList(cb0,cb1,cb2,cb3) );

    RooArgList cbpars(cb0,cb1,cb2);
    //if(mode==Pi0EE) cbpars.add( cb4);
    //if(mode==Pi0EE) cbpars.add( cb5);

    // try to use a second order polynomial, if the fit is bad add other terms
    // if you start with many terms, the fit creates strange curvy shapes trying to fit the statistical fluctuations
    // 2nd order means a curve with no change of concavity
    
    if(niter==1){
      cbpars.add( cb3);
    }
    if(niter==2){
      cb3.setRange(-1,1.);
      cb4.setRange(-0.3,0.3);
      cbpars.add( cb3);
      cbpars.add( cb4 );     
    }
    if(niter==3){
      cb3.setRange(-1,1.);
      cb4.setRange(-1,1);
      cb5.setRange(-0.5, 0.5);
      cbpars.add( cb3);
      cbpars.add( cb4 );
      cbpars.add( cb5 );
    }

    RooChebychev bkg("bkg","bkg model", x, cbpars );

    //RooPolynomial bkg("bkg","background model",x,RooArgList(p0,p1,p2,p3,p4,p5,p6) );
    //RooPolynomial bkg("bkg","background model",x,RooArgList(p0,p1,p2,p3) );

    //RooRealVar Nbkg("Nbkg","background yield",1.e3,0.,1.e8);
    RooRealVar Nbkg("Nbkg","background yield",h->Integral()*0.85,0.,h->Integral()*10.0);
    //Nbkg.setVal( h->GetSum()*0.8 );

    RooAbsPdf* model=0;

    RooAddPdf model1("model","sig+bkg",RooArgList(gaus,bkg),RooArgList(Nsig,Nbkg));
    RooAddPdf model2("model","sig+bkg",RooArgList(signal,bkg),RooArgList(Nsig,Nbkg));

    if(ngaus==1)      model = &model1;
    else if(ngaus==2) model = &model2;


    RooNLLVar nll("nll","log likelihood var",*model,dh, RooFit::Extended(true));
    //RooAbsReal * nll = model->createNLL(dh); //suggetsed way, taht should be the same

    RooFitResult* res = nullptr;
    RooMinuit m(nll);
    RooMinimizer mfit(nll);

    if (useFit_RooMinuit_) {

      // // original fit
      // // obsolete: see here --> https://root-forum.cern.ch/t/roominuit-and-roominimizer-difference/18230/8
      // // better to use RooMinimizer, but please read caveat below
      m.setVerbose(kFALSE);
      //m.setVerbose(kTRUE);
      m.migrad();
      m.hesse();  // sometimes it fails, caution
      res = m.save() ;

    } else {

      // alternative fit (results are pretty much the same)
      // IMPORTANT, READ CAREFULLY: sometimes this method fails.
      // This happens because at the boundaries of the fit range the pdf goea slightly below 0 (so it is negative). The fitter tries to cope wth it and should tipically
      // manage to converge. However, I noticed that after few attemps (even though the default number of attemps should be several hundreds or thousands of times) 
      // the job crashes, and this seems to be a feature of cmssw, not of RooFit
      // The reason why the pdf gets negative could be due to the fact that, regardless the chosen fit range given by xlo and xhi, the actual fit range goes from the 
      // lower edge of the leftmost bin containing xlo to the upper edge of the rightmost one containing xhi, but then the fit tries to "pass" across the bin centers
      // Therefore, for a sharply rising (or falling) distribution, the pdf can become negative
      // The consequence is that there are large areas in the calibration map of related 2D plots that are white (because the fit there was not done succesfully)
      // The previous method using RooMinuit seems to be more robust, so I suggest we should use that one even though it is said to be obsolete
      mfit.setVerbose(kFALSE);
      mfit.setPrintLevel(-1);
      mfit.setStrategy(2);  // 0,1,2:  MINUIT strategies for dealing most efficiently with fast FCNs (0), expensive FCNs (2) and 'intermediate' FCNs (1)
      //cout << "FIT_EPSILON: Minimize" << endl;
      mfit.minimize("Minuit2","minimize");
      //cout << "FIT_EPSILON: Minimize hesse " << endl;
      mfit.minimize("Minuit2","hesse");
      //cout<<"FIT_EPSILON: Estimate minos errors for all parameters"<<endl;
      mfit.minos(RooArgSet(Nsig,Nbkg,mean));
      res = mfit.save() ;

    }

    RooChi2Var chi2("chi2","chi2 var",*model,dh, true);
    // use only bins in fit range for ndof (dh is made with var x that already has the restricted range, but h is the full histogram)
    //int ndof = h->GetNbinsX() - res->floatParsFinal().getSize();
    int ndof = h->FindFixBin(xhi) - h->FindFixBin(xlo) +1 - res->floatParsFinal().getSize(); 

    //compute S/B and chi2
    x.setRange("sobRange",mean.getVal()-3.*sigma.getVal(), mean.getVal()+3.*sigma.getVal());
    RooAbsReal* integralSig = gaus.createIntegral(x,NormSet(x),Range("sobRange"));

    RooAbsReal* integralBkg = bkg.createIntegral(x,NormSet(x),Range("sobRange"));

    float normSig = integralSig->getVal();
    float normBkg = integralBkg->getVal();

    Pi0FitResult pi0res; // this is the output value of this method
    pi0res.res = res;

    pi0res.S = normSig*Nsig.getVal();
    pi0res.Serr = normSig*Nsig.getError();

    pi0res.B = normBkg*Nbkg.getVal();
    pi0res.Berr = normBkg*Nbkg.getError();

    pi0res.SoB =  pi0res.S/pi0res.B;
    pi0res.SoBerr =  pi0res.SoB*sqrt( pow(pi0res.Serr/pi0res.S,2) + 
		pow(pi0res.Berr/pi0res.B,2) ) ;
    pi0res.dof = ndof;
    pi0res.nFitParam = res->floatParsFinal().getSize();


    RooPlot*  xframe = x.frame(h->GetNbinsX());
    //RooPlot*  xframe = x.frame(xlo, xhi);
    xframe->SetName((nameHistofit+Form("_rp")).Data());
    xframe->SetTitle(h->GetTitle());
    dh.plotOn(xframe, Name("data"));
    model->plotOn(xframe,Components(bkg),LineStyle(kDashed), LineColor(kRed), Name("bkgOnly"));
    model->plotOn(xframe,Components(gaus),LineStyle(kDashed), LineColor(kGreen+1), Name("sigOnly"));
    model->plotOn(xframe, Name("model"));

    // TMAth::Prob() uses Chi2, not reduced Chi2, while xframe->chiSquare() returns the reduced Chi2
    pi0res.chi2 = xframe->chiSquare("model","data",pi0res.nFitParam) * pi0res.dof;
    pi0res.probchi2 = TMath::Prob(pi0res.chi2, ndof);

    xframe->Draw();

    cout << "FIT_EPSILON: Nsig: " << Nsig.getVal() 
	 << " nsig 3sig: " << normSig*Nsig.getVal()
	 << " nbkg 3sig: " << normBkg*Nbkg.getVal()
	 << " S/B: " << pi0res.SoB << " +/- " << pi0res.SoBerr
	 << " chi2: " << pi0res.chi2
	 << " chi2 reduced: " << pi0res.chi2 / pi0res.dof
	 << " DOF: " << pi0res.dof
	 << " N(fit.param.): " << pi0res.nFitParam
	 << " prob(chi2): " << pi0res.probchi2
	 << endl;

    if(mode==Pi0EB){
	  EBmap_Signal[HistoIndex]=pi0res.S;
	  EBmap_Backgr[HistoIndex]=pi0res.B;
	  EBmap_Chisqu[HistoIndex]=xframe->chiSquare();
	  EBmap_ndof[HistoIndex]=ndof;
	  EBmap_mean[HistoIndex]=mean.getVal();
	  EBmap_mean_err[HistoIndex]=mean.getError();
	  EBmap_sigma[HistoIndex]=sigma.getVal();
	  EBmap_Snorm[HistoIndex]=normSig;
	  EBmap_b0[HistoIndex]=cb0.getVal();
	  EBmap_b1[HistoIndex]=cb1.getVal();
	  EBmap_b2[HistoIndex]=cb2.getVal();
	  EBmap_b3[HistoIndex]=cb3.getVal();
	  EBmap_Bnorm[HistoIndex]=normBkg;
    }
    if(mode==Pi0EE){
	  EEmap_Signal[HistoIndex]=pi0res.S;
	  EEmap_Backgr[HistoIndex]=pi0res.B;
	  EEmap_Chisqu[HistoIndex]=xframe->chiSquare();
	  EEmap_ndof[HistoIndex]=ndof;
	  EEmap_mean[HistoIndex]=mean.getVal();
	  EEmap_mean_err[HistoIndex]=mean.getError();
	  EEmap_sigma[HistoIndex]=sigma.getVal();
	  EEmap_Snorm[HistoIndex]=normSig;
	  EEmap_b0[HistoIndex]=cb0.getVal();
	  EEmap_b1[HistoIndex]=cb1.getVal();
	  EEmap_b2[HistoIndex]=cb2.getVal();
	  EEmap_b3[HistoIndex]=cb3.getVal();
	  EEmap_Bnorm[HistoIndex]=normBkg;
    }

    TLatex lat;
    std::string line = "";
    lat.SetNDC();
    lat.SetTextSize(0.040);
    lat.SetTextColor(1);

    float xmin(0.58), yhi(0.80), ypass(0.05);
    if(mode==EtaEB) yhi=0.30;
    if(mode==Pi0EE) yhi=0.5;
    line = Form("Yield: %.0f #pm %.0f", Nsig.getVal(), Nsig.getError() );
    lat.DrawLatex(xmin,yhi, line.c_str());

    line = Form("m_{#gamma#gamma}: %.2f #pm %.2f", mean.getVal()*1000., mean.getError()*1000. );
    lat.DrawLatex(xmin,yhi-ypass, line.c_str());

    line = Form("#sigma: %.2f #pm %.2f (%.2f%s)", sigma.getVal()*1000., sigma.getError()*1000., sigma.getVal()*100./mean.getVal(), "%" );
    lat.DrawLatex(xmin,yhi-2.*ypass, line.c_str());

    //sprintf(line,"S/B(3#sigma): %.2f #pm %.2f", pi0res.SoB, pi0res.SoBerr );
    line = Form("S/B(3#sigma): %.2f", pi0res.SoB );
    lat.DrawLatex(xmin,yhi-3.*ypass, line.c_str());

    line = Form("#Chi^{2}: %.2f (%d dof)", pi0res.chi2, pi0res.dof );
    lat.DrawLatex(xmin,yhi-4.*ypass, line.c_str());

    line = Form("B param. %d", cbpars.getSize() );
    lat.DrawLatex(xmin,yhi-5.*ypass, line.c_str());

    canvas->RedrawAxis("sameaxis");

    Pi0FitResult fitres = pi0res;
    //xframe->chiSquare() is the chi2 reduced, i.e., that whose expected value is 1
    // E[X^2]=v; Var[X^2]=2v --> fit is bad if |X^2-v|>5*sqrt(2v) 

    //if(mode==Pi0EB && ( xframe->chiSquare()/pi0res.dof>0.35 || pi0res.SoB<0.6 || fabs(mean.getVal()-(Are_pi0_? 0.150:0.62))<0.0000001 ) ){
    //bool badChi2 = fabs(xframe->chiSquare() - pi0res.dof) > 5.0 * sqrt(2. * pi0res.dof);

    if(mode==Pi0EB && ( fabs(mean.getVal()-maxMassForGaussianMean)<0.0000001 ) ){
	  if(niter==0) fitres = FitMassPeakRooFit( h, xlo, xhi, HistoIndex, ngaus, mode, 1, isNot_2010_);
	  if(niter==1) fitres = FitMassPeakRooFit( h, xlo, xhi, HistoIndex, ngaus, mode, 2, isNot_2010_);
	  if(niter==2) fitres = FitMassPeakRooFit( h, xlo, xhi, HistoIndex, ngaus, mode, 3, isNot_2010_);
    }
    if(mode==Pi0EE && ( fabs(mean.getVal()-maxMassForGaussianMean)<0.0000001 ) ){
	  if(niter==0) fitres = FitMassPeakRooFit( h, xlo, xhi, HistoIndex, ngaus, mode, 1, isNot_2010_);
	  if(niter==1) fitres = FitMassPeakRooFit( h, xlo, xhi, HistoIndex, ngaus, mode, 2, isNot_2010_);
	  if(niter==2) fitres = FitMassPeakRooFit( h, xlo, xhi, HistoIndex, ngaus, mode, 3, isNot_2010_);
    }

    // save last version of fit made
    // if(StoreForTest_ && niter==0){
    if(StoreForTest_){
      outfileTEST_->cd();
      xframe->Write();
      canvas->Write();
    }

    delete canvas;
    return fitres;
}


//------------------------------------------------
// method to fit E/Etrue

//-----------------------------------------------------------------------------------

//=====================================================================

Float_t my2sideCrystalBall(double* x, double* par) {

  // implementation of a 2-sided crystal ball
  //a priori we allow for different shape of right and left tail, thus two values of alpha and n 

  Float_t xcur = x[0];
  Float_t N = par[0];
  Float_t mu = par[1];
  Float_t sigma = par[2];
  Float_t alphaL = par[3];
  Float_t nL = par[4];
  Float_t alphaR = par[5];
  Float_t nR = par[6];
  Float_t t = (xcur-mu)/sigma;
  Float_t absAlphaL = fabs((Float_t)alphaL);
  Float_t invAbsAlphaL = 1./absAlphaL;
  Float_t absAlphaR = fabs((Float_t)alphaR);
  Float_t invAbsAlphaR = 1./absAlphaR;

  if ( t<-absAlphaL ) {
    Float_t AL = TMath::Power(nL*invAbsAlphaL,nL)*exp(-0.5*absAlphaL*absAlphaL);
    Float_t BL = nL*invAbsAlphaL - absAlphaL;
    return N*AL*TMath::Power(BL-t,-nL);
  } else if ( t <= absAlphaR )  {
    return N*exp(-0.5*t*t);
  } else {
    Float_t AR = TMath::Power(nR*invAbsAlphaR,nR)*exp(-0.5*absAlphaR*absAlphaR);
    Float_t BR = nR*invAbsAlphaR - absAlphaR;
    return N*AR*TMath::Power(BR+t,-nR);
  }

}

//=====================================================================

Float_t myLeftTailCrystalBall(double* x, double* par) {

  // implementation of a left-tail crystal ball

  Float_t xcur = x[0];
  Float_t N = par[0];
  Float_t mu = par[1];
  Float_t sigma = par[2];
  Float_t alphaL = par[3];
  Float_t nL = par[4];
  Float_t t = (xcur-mu)/sigma;
  Float_t absAlphaL = fabs((Float_t)alphaL);
  Float_t invAbsAlphaL = 1./absAlphaL;

  if ( t<-absAlphaL ) {
    Float_t AL = TMath::Power(nL*invAbsAlphaL,nL)*exp(-0.5*absAlphaL*absAlphaL);
    Float_t BL = nL*invAbsAlphaL - absAlphaL;
    return N*AL*TMath::Power(BL-t,-nL);
  } else {
    return N*exp(-0.5*t*t);
  }

}

//=====================================================================

Float_t myRightTailCrystalBall(double* x, double* par) {

  // implementation of a right-tail crystal ball

  Float_t xcur = x[0];
  Float_t N = par[0];
  Float_t mu = par[1];
  Float_t sigma = par[2];
  Float_t alphaR = par[3];
  Float_t nR = par[4];
  Float_t t = (xcur-mu)/sigma;
  Float_t absAlphaR = fabs((Float_t)alphaR);
  Float_t invAbsAlphaR = 1./absAlphaR;

  if ( t>absAlphaR ) {
    Float_t AR = TMath::Power(nR*invAbsAlphaR,nR)*exp(-0.5*absAlphaR*absAlphaR);
    Float_t BR = nR*invAbsAlphaR - absAlphaR;
    return N*AR*TMath::Power(BR+t,-nR);
  } else {
    return N*exp(-0.5*t*t);
  }

}


//======================================================

TFitResultPtr FitEpsilonPlot::FitEoverEtruePeak(TH1F* h1, Bool_t isSecondGenPhoton, uint32_t HistoIndex, FitMode mode, Bool_t noDrawStatBox) 
{

  int nPhoton = isSecondGenPhoton ? 2 : 1;

  bool fitDoubleCrystalBall = true; // FIXME: set manually, to be set in parameters.py
  bool fitSingleCrystalBall = false;
  //float integralInRange = h1->Integral(h1->GetXaxis()->FindFixBin(0.6), h1->GetXaxis()->FindFixBin(1.1));
  float integralInRange = h1->Integral();
  if ( integralInRange > std::min(100.0, 4.0 * EoverEtrue_integralMin)) {
    fitSingleCrystalBall = true;
  } else {
    std::cout << "FIT_EPSILON: photon " << nPhoton << " --> integral=" << integralInRange << ": fit with gaussian only" << std::endl;
  }
  bool fitCrystalBall = false;
  if (fitDoubleCrystalBall || fitSingleCrystalBall) fitCrystalBall = true;
  //fitCrystalBall = false;

  //-----------------------------------------------------------------------------------
  // For the moment we use the TH1::Fit function here [0] instead of RooFit for simplicity
  // [0] https://root.cern.ch/doc/master/classTH1.html#a7e7d34c91d5ebab4fc9bba3ca47dabdd

  // std::cout << "FitEpsilonPlot::FitEoverEtruePeak called " << std::endl;

  int niter = 0; // attempt of the fit, only 1 for the moment
  TString nameHistofit = Form("Fit_n_%u_attempt%d_g%d",HistoIndex,niter,nPhoton);

  // add canvas to save rooplot on top (will save this in the file)
  TCanvas* canvas = new TCanvas((nameHistofit+Form("_c")).Data(),"",700,700);
  canvas->cd();
  canvas->SetTickx(1);
  canvas->SetTicky(1);
  canvas->cd();
  canvas->SetRightMargin(0.06);

  gPad->Update();
  //gStyle->SetOptStat(1110);
  gStyle->SetOptStat(0);
  gStyle->SetOptFit(1102);

  h1->Draw("EP");
  //h1->GetXaxis()->SetTitle(Form("photon %d E(reco)/E(true)",nPhoton));
  h1->GetXaxis()->SetTitle(Form(" #gamma_{%d} E_{reco}/E_{true}",nPhoton));
  h1->GetXaxis()->SetTitleSize(0.04);
  h1->GetYaxis()->SetTitle("Events");
  h1->SetLineColor(kBlack);
  h1->SetMarkerColor(kBlack);
  h1->SetMarkerStyle(20); // 20 is big dot, 7 is smaller dot
  h1->SetMarkerSize(1); // it has no effect when using dot given by marker number 1, 6 or 7

  Double_t histNorm = h1->GetBinContent(h1->GetMaximumBin());  //cout << "histNorm = " << histNorm << endl;
  //Double_t histMean = h1->GetMean();   //  cout << "histMean = " << histMean << endl;
  Double_t histMean = h1->GetBinCenter(h1->GetMaximumBin());   //cout << "histMean = " << histMean << endl;
  // tails are huge, the Std deviation is not a good estimator of the gaussian core's width, use a constant term (but in general it will depend on the crystal)
  //Double_t histStdDev = h1->GetStdDev();  //cout << "histStdDev = " << histStdDev << endl;
  Double_t histStdDev = isSecondGenPhoton ? 0.15 : 0.1;
  if (histMean < 0.6) histMean = 0.9;


  // // for photon 2 the tail can have a peak larger than the peak around 1 !!
  // // so do not blindly use the maximum bin to estimate the peak position
  // Double_t histNorm = 0.0;
  // Double_t histMean = 0.0;
  // Double_t histStdDev = 0.0;

  // TH1F* htmp = (TH1F*) h1->Clone("h1_clone");
  // htmp->GetXaxis()->SetRange(0.7,1.1);
  // histNorm = htmp->GetBinContent(htmp->GetMaximumBin());
  // histMean = htmp->GetBinCenter(htmp->GetMaximumBin()); 
  // //histMean = htmp->GetMean(); 
  // histStdDev = htmp->GetStdDev();
  // if (isSecondGenPhoton && histStdDev > 0.15) histStdDev = 0.15;
  // else if (histStdDev > 0.1) histStdDev = 0.1;
  // if (histMean < 0.6) histMean = 0.9;

  int fitStatus = -1;
  // do a preliminary gaussian fit, but do not draw the function (option 0)
  TF1 *gaussian = new TF1("gaussian","gaus",histMean-4.0*histStdDev, histMean+4.0*histStdDev);
  gaussian->SetParLimits(0, 0.9 * histNorm, 1.1 * histNorm);
  gaussian->SetParLimits(1, histMean - histStdDev, histMean + histStdDev);
  gaussian->SetParLimits(2, 0.05 * histStdDev, 2.0 * histStdDev);
  gaussian->SetParameter(0, histNorm);
  gaussian->SetParameter(1, histMean);
  gaussian->SetParameter(2, histStdDev);
  gaussian->SetLineColor(kRed);
  gaussian->SetLineWidth(2);
  string gausFitOption = "E WL S Q B R";  // ad E and M for better error and minimum estimate
  string gausDrawOptions = fitCrystalBall ? "" : "HE SAMES";
  string crystalBallFitOption = "E WL S Q B R";
  if (fitCrystalBall) gausFitOption += " 0"; // will not draw this gaussian
  // TFitResultPtr frp1 = h1->Fit("gaus",gausFitOption.c_str(),"", histMean - 0.8 * histStdDev, histMean + 1.0 * histStdDev);
  TFitResultPtr frp1 = h1->Fit(gaussian,gausFitOption.c_str(),gausDrawOptions.c_str(), histMean - 0.8 * histStdDev, histMean + 1.0 * histStdDev);
  // cout << "checkpoint after fitting with gaussian" << endl; //return 0;
  // TF1 *mygaussian = nullptr;
  // if (not fitDoubleCrystalBall) {
  //   mygaussian = h1->GetFunction("gaus");
  //   if (mygaussian) {
  //     mygaussian->SetLineColor(kRed);
  //     mygaussian->SetLineWidth(2);
  //   //mygaussian->Draw("SAME");
  //   }
  // }
  fitStatus = frp1;
  // if gaussian fit was successful, update the gaussian mean and width values that will be used for the crystal ball below
  if (fitStatus != 0) {
    std::cout << "FIT_EPSILON: photon " << nPhoton << " --> error occurred in FitEoverEtruePeak when fitting with gaussian. Fit status is " << fitStatus << std::endl;
  } else {
    std::cout << "FIT_EPSILON: photon " << nPhoton << " --> Fit status is " << fitStatus << " for gaussian fit " << std::endl;
  }
  histMean = frp1->Parameter(1);  // par [2] is the gaussian sigma in ROOT
  histStdDev = frp1->Parameter(2);  // par [2] is the gaussian sigma in ROOT

  // cout << "checkpoint after gaussian fit status evaluation" << endl; //return 0;

  // define the fitting function
  TF1*doubleCB = new TF1("doubleCB",&my2sideCrystalBall,histMean-5*histStdDev, histMean+5*histStdDev,7);
  doubleCB->SetParNames("Const","Mean(CB)","Sigma(CB)","alphaL","nL","alphaR","nR");
  doubleCB->SetParLimits(doubleCB->GetParNumber("nL"),0.1,15);
  doubleCB->SetParLimits(doubleCB->GetParNumber("Mean(CB)"), histMean - histStdDev, histMean + histStdDev);
  doubleCB->SetParLimits(doubleCB->GetParNumber("Sigma(CB)"),0.1 * histStdDev, 1.1 * histStdDev);
  doubleCB->SetParLimits(doubleCB->GetParNumber("nR"),0.1,15);
  doubleCB->SetParLimits(doubleCB->GetParNumber("alphaL"),-3.0,-0.1);
  doubleCB->SetParLimits(doubleCB->GetParNumber("alphaR"),0.1,3.0);
  doubleCB->SetParLimits(doubleCB->GetParNumber("Const"),0.8*histNorm,1.2*histNorm);
  doubleCB->SetParameters(histNorm,histMean,histStdDev,-1.0,5,1.0,5);
  doubleCB->SetLineColor(kGreen+2);
  doubleCB->SetLineWidth(2);

  TF1*leftCB = new TF1("leftCB",&myLeftTailCrystalBall,histMean-5*histStdDev, histMean+5*histStdDev,5);
  leftCB->SetParNames("Const","Mean(CB)","Sigma(CB)","alphaL","nL");
  leftCB->SetParLimits(leftCB->GetParNumber("nL"),0.1,15);
  leftCB->SetParLimits(leftCB->GetParNumber("Mean(CB)"), histMean - histStdDev, histMean + histStdDev);
  leftCB->SetParLimits(leftCB->GetParNumber("Sigma(CB)"),0.1 * histStdDev, 2.0 * histStdDev);
  leftCB->SetParLimits(leftCB->GetParNumber("alphaL"),-3.0,-0.1);
  leftCB->SetParLimits(leftCB->GetParNumber("Const"),0.8*histNorm,1.2*histNorm);
  leftCB->SetParameters(histNorm,histMean,histStdDev,-1.0,5);
  leftCB->SetLineColor(kGreen+2);
  leftCB->SetLineWidth(2);

  TF1*rightCB = new TF1("rightCB",&myRightTailCrystalBall,histMean-5*histStdDev, histMean+5*histStdDev,5);
  rightCB->SetParNames("Const","Mean(CB)","Sigma(CB)","alphaR","nR");
  rightCB->SetParLimits(rightCB->GetParNumber("nR"),0.1,15);
  rightCB->SetParLimits(rightCB->GetParNumber("Mean(CB)"), histMean - histStdDev, histMean + histStdDev);
  rightCB->SetParLimits(rightCB->GetParNumber("Sigma(CB)"),0.1 * histStdDev, 2.0 * histStdDev);
  rightCB->SetParLimits(rightCB->GetParNumber("alphaR"),0.1,3.0);
  rightCB->SetParLimits(rightCB->GetParNumber("Const"),0.8*histNorm,1.2*histNorm);
  rightCB->SetParameters(histNorm,histMean,histStdDev,1.0,5);
  rightCB->SetLineColor(kGreen+2);
  rightCB->SetLineWidth(2);


  TFitResultPtr frp2;
  TF1 *gaussCore = nullptr;
  TF1 *fittedCrystalBall = nullptr;

  if (fitCrystalBall) {

    fittedCrystalBall = (fitDoubleCrystalBall ? doubleCB : rightCB);
    if (fitDoubleCrystalBall) frp2 = h1->Fit(fittedCrystalBall,crystalBallFitOption.c_str(),"HE SAMES", histMean - 0.8 * histStdDev, histMean + 1.8 * histStdDev);
    else                      frp2 = h1->Fit(fittedCrystalBall,crystalBallFitOption.c_str(),"HE SAMES", histMean - 0.8 * histStdDev, histMean + 2.0 * histStdDev);
    // cout << "checkpoint after crystal ball" << endl; //return 0;
    cout << "check point " << endl;

    fitStatus = frp2;
    if (fitStatus != 0) {
      std::cout << "FIT_EPSILON: photon " << nPhoton << " -->  error occurred in FitEoverEtruePeak when fitting with crystal ball. Fit status is " << fitStatus << std::endl;
    } else {
      std::cout << "FIT_EPSILON: photon " << nPhoton << " --> Fit status is " << fitStatus << " for crystal ball fit " << std::endl;
    }
    if (frp2->Parameter(doubleCB->GetParNumber("Sigma(CB)")) < 0.0 ) {
      cout << "WARNING: CB sigma is negative!" << endl;
    }

    // get gaussian core of the CB and plot it on top to show how the CB differs from a simple gaussian
    gaussCore = new TF1(*(h1->GetFunction(fittedCrystalBall->GetName())));
    if (gaussCore) {
      Double_t gaussMean =  frp2->Parameter(fittedCrystalBall->GetParNumber("Mean(CB)"));
      Double_t gaussSigma = frp2->Parameter(fittedCrystalBall->GetParNumber("Sigma(CB)"));
      Double_t alphaL =     frp2->Parameter(fittedCrystalBall->GetParNumber("alphaL"));
      Double_t alphaR =     0;
      if (fitDoubleCrystalBall) {
	frp2->Parameter(fittedCrystalBall->GetParNumber("alphaR"));
	gaussCore->DrawF1(gaussMean + fabs(gaussSigma) * -fabs(alphaL), gaussMean + fabs(gaussSigma) * fabs(alphaR),"SAME"); // alphaL < 0, alphaR > 0
      } else {
	gaussCore->DrawF1(gaussMean + fabs(gaussSigma) * -fabs(alphaL), histMean + 1.5 * histStdDev,"SAME"); // alphaL < 0, alphaR > 0
      }
      gaussCore->SetLineColor(kRed);
      gaussCore->SetLineWidth(2);
    }

  }

  TFitResultPtr retfrp = (fitCrystalBall ? frp2 : frp1);;

  canvas->Update();
  TPaveStats *statBox = (TPaveStats*)(h1->FindObject("stats"));
  if (statBox) {
    statBox->SetX1NDC(0.12);   
    statBox->SetX2NDC(0.42);
    statBox->SetY1NDC(0.3);
    statBox->SetY2NDC(0.64);
    statBox->SetFillColor(0);
    statBox->SetFillStyle(0);
    statBox->SetBorderSize(0);
    statBox->Draw();
  } else {
    cout << "Couldn't get statbox with (TPaveStats*)(h1->FindObject(\"stats\"))" << endl;
  }
  canvas->Update();

  string legHeader = "";//"Fit components";
  TLegend leg (0.12,0.69,0.45,0.89);
  if (legHeader != "") leg.SetHeader(legHeader.c_str());
  leg.SetFillColor(0);
  leg.SetFillStyle(0);
  leg.SetBorderSize(0);
  leg.AddEntry(h1,"data","PLE");
  //leg.AddEntry(gaussian,"gauss","l");
  if (fitCrystalBall) {
    if (gaussCore) leg.AddEntry(gaussCore,"gaussian core","l");
    if (fitDoubleCrystalBall) leg.AddEntry(doubleCB,"double Crystal Ball","l");
    else                      leg.AddEntry(rightCB,"right Crystal Ball","l");
  } else {
    //leg.AddEntry(mygaussian,"gaussian","l");
    leg.AddEntry(gaussian,"gaussian","l");
  }
  leg.Draw("same");
  canvas->RedrawAxis("sameaxis");
  
  // if (noDrawStatBox) {
  //   h1->SetStats(0);
  //   //cout << "No Statistics box" << endl;
  // } else {
  //   //canvas->Update();
  //   gPad->Update();
  //   gStyle->SetOptStat(1110);
  //   gStyle->SetOptFit(1102);
  // }

  // gPad->Update();
  // //gStyle->SetOptStat(1110);
  // gStyle->SetOptStat(0);
  // gStyle->SetOptFit(1102);
  h1->SetStats(0);

  if (fitCrystalBall) {
    cout << "FIT_EPSILON: "
	 << "photon " << nPhoton << "  "
	 << " mean(CB): " << frp2->Parameter(doubleCB->GetParNumber("Mean(CB)")) << " +/- " << frp2->ParError(doubleCB->GetParNumber("Mean(CB)"))
	 << " sigma(CB): " << frp2->Parameter(doubleCB->GetParNumber("Sigma(CB)")) << " +/- " << frp2->ParError(doubleCB->GetParNumber("Sigma(CB)"))
	 << " chi2: " << frp2->Chi2()
	 << " DOF: " << frp2->Ndf()
	 << " N(fit.param.): " << frp2->NFreeParameters()
	 << " prob(chi2): " << frp2->Prob()
	 << endl;
  } else {
    cout << "FIT_EPSILON: "
	 << "photon " << nPhoton << "  "
	 << " mean(gauss): " << frp1->Parameter(1) << " +/- " << frp1->ParError(1)
	 << " sigma(gauss): " << frp1->Parameter(2) << " +/- " << frp1->ParError(2)
	 << " chi2: " << frp1->Chi2()
	 << " DOF: " << frp1->Ndf()
	 << " N(fit.param.): " << frp1->NFreeParameters()
	 << " prob(chi2): " << frp1->Prob()
	 << endl;
  }

  // some parameters do not make sense for the E/Etrue study, but for simplicity we keep the same structure as the mass fit
  // basically we just need the peak position ("Mean(CB)" parameter)
  // let's use fitresptr_g1 and fitresptr_g2 to store all the required information, without doubling the maps

  if(mode==Pi0EB) {

    if (isSecondGenPhoton) EBmap_fitresptr_g2[HistoIndex]=retfrp;
    else                   EBmap_fitresptr_g1[HistoIndex]=retfrp;
  //   // EBmap_Signal[HistoIndex]=-1;
  //   // EBmap_Backgr[HistoIndex]=-1;
  //   EBmap_Chisqu[HistoIndex]=frp2->Chi2();
  //   EBmap_ndof[HistoIndex]=frp2->Ndf();
  //   EBmap_mean[HistoIndex]=frp2->Parameter(doubleCB->GetParNumber("Mean(CB)"));
  //   EBmap_mean_err[HistoIndex]=frp2->ParError(doubleCB->GetParNumber("Mean(CB)"));
  //   EBmap_sigma[HistoIndex]=frp2->Parameter(doubleCB->GetParNumber("Sigma(CB)"));;
  //   // EBmap_Snorm[HistoIndex]=-1;
  //   // EBmap_b0[HistoIndex]=-1;
  //   // EBmap_b1[HistoIndex]=-1;
  //   // EBmap_b2[HistoIndex]=-1;
  //   // EBmap_b3[HistoIndex]=-1;
  //   // EBmap_Bnorm[HistoIndex]=-1;

  }

  if(mode==Pi0EE) {

    if (isSecondGenPhoton) EEmap_fitresptr_g2[HistoIndex]=retfrp;
    else                   EEmap_fitresptr_g1[HistoIndex]=retfrp;
  //   // EEmap_Signal[HistoIndex]=-1;
  //   // EEmap_Backgr[HistoIndex]=-1;
  //   EEmap_Chisqu[HistoIndex]=frp2->Chi2();
  //   EEmap_ndof[HistoIndex]=frp2->Ndf();
  //   EEmap_mean[HistoIndex]=frp2->Parameter(doubleCB->GetParNumber("Mean(CB)"));
  //   EEmap_mean_err[HistoIndex]=frp2->ParError(doubleCB->GetParNumber("Mean(CB)"));
  //   EEmap_sigma[HistoIndex]=frp2->Parameter(doubleCB->GetParNumber("Sigma(CB)"));;
  //   // EEmap_Snorm[HistoIndex]=-1;
  //   // EEmap_b0[HistoIndex]=-1;
  //   // EEmap_b1[HistoIndex]=-1;
  //   // EEmap_b2[HistoIndex]=-1;
  //   // EEmap_b3[HistoIndex]=-1;
  //   // EEmap_Bnorm[HistoIndex]=-1;

  }


  // if(StoreForTest_ && niter==0){
  if(StoreForTest_){
    outfileTEST_->cd();
    canvas->Write();
  }

  delete canvas;
  return retfrp;

}


//=============================================

My_double_CB::My_double_CB(const char *name, const char *title, 
			   RooAbsReal& _x,
			   RooAbsReal& _mu,
			   RooAbsReal& _sig,
			   RooAbsReal& _a1,
			   RooAbsReal& _n1,
			   RooAbsReal& _a2,
			   RooAbsReal& _n2) :
RooAbsPdf(name,title), 
	    x("x","x",this,_x),
	    mu("mu","mu",this,_mu),
	    sig("sig","sig",this,_sig),
	    a1("a1","a1",this,_a1),
	    n1("n1","n1",this,_n1),
	    a2("a2","a2",this,_a2),
	    n2("n2","n2",this,_n2)
{ 
} 


My_double_CB::My_double_CB(const My_double_CB& other, const char* name) :  
  RooAbsPdf(other,name), 
  x("x",this,other.x),
  mu("mu",this,other.mu),
  sig("sig",this,other.sig),
  a1("a1",this,other.a1),
  n1("n1",this,other.n1),
  a2("a2",this,other.a2),
  n2("n2",this,other.n2)
{ 
} 



Double_t My_double_CB::evaluate() const 
{ 
  double u   = (x-mu)/sig;
  double A1  = TMath::Power(n1/TMath::Abs(a1),n1)*TMath::Exp(-a1*a1/2);
  double A2  = TMath::Power(n2/TMath::Abs(a2),n2)*TMath::Exp(-a2*a2/2);
  double B1  = n1/TMath::Abs(a1) - TMath::Abs(a1);
  double B2  = n2/TMath::Abs(a2) - TMath::Abs(a2);

  double result(1);
  if      (u<-a1) result *= A1*TMath::Power(B1-u,-n1);
  else if (u<a2)  result *= TMath::Exp(-u*u/2);
  else            result *= A2*TMath::Power(B2+u,-n2);
  return result;
} 


//======================================================

Pi0FitResult FitEpsilonPlot::FitEoverEtruePeakRooFit(TH1F* h1, Bool_t isSecondGenPhoton, uint32_t HistoIndex, FitMode mode) 
{

  // some flags for the fit
  // usign the double Crystal Ball seems the better choice, but might require some parameters tuning
  bool noFitBkg = true;  // use only signal model for the fit
  bool useCBtoFit = isSecondGenPhoton ? true : false;  // use Crystal Ball (tail orientation depends on the parameter alpha given below)
  //  bool useCB2toFit = isSecondGenPhoton ? true : false;
  bool useCB2toFit = true;   // use double Crystal Ball (overrides useCBtoFit)

  // std::cout << "FitEpsilonPlot::FitEoverEtruePeak called " << std::endl;
  int nPhoton = isSecondGenPhoton ? 2 : 1;

  int niter = 0; // attempt of the fit, only 1 for the moment
  TString nameHistofit = Form("Fit_n_%u_attempt%d_g%d",HistoIndex,niter,nPhoton);

  // add canvas to save rooplot on top (will save this in the file)
  TCanvas* canvas = new TCanvas((nameHistofit+Form("_c")).Data(),"",700,600);
  canvas->cd();
  canvas->SetTickx(1);
  canvas->SetTicky(1);
  canvas->cd();
  canvas->SetRightMargin(0.06);

  // get RMS in narrow range around the peak
  TH1F* h1narrow = new TH1F("h1narrow","",
			    1 + h1->FindFixBin(1.1) - h1->FindFixBin(0.8), 
			    h1->GetBinLowEdge(h1->FindFixBin(0.8)), 
			    h1->GetBinLowEdge(1 + h1->FindFixBin(1.1)));
  for (int i = 0; i <= h1narrow->GetNbinsX(); i++) {
    h1narrow->SetBinContent(i,h1->GetBinContent(h1->FindFixBin(h1narrow->GetBinCenter(i))));
  }
  float rmsh1narrow = h1narrow->GetStdDev();
  float xmaxbin = h1narrow->GetBinCenter(h1narrow->GetMaximumBin());
  delete h1narrow;


  // fit range, allow for differences between two photons
  // float_t xlo = isSecondGenPhoton ? 0.82 : 0.82;
  float_t xlo = std::max(0.82, isSecondGenPhoton ? (xmaxbin - 1.6 * rmsh1narrow) : (xmaxbin - 1.7 * rmsh1narrow));
  //float_t xhi = isSecondGenPhoton ? 1.15 : 1.15;
  float_t xhi = std::min(1.15, isSecondGenPhoton ? (xmaxbin + 2.0 * rmsh1narrow) : (xmaxbin + 2.2 * rmsh1narrow));

  RooRealVar x("x",Form("#gamma %d E/E_{true}",nPhoton), 0.0, 1.5, "");
  RooDataHist dh("dh",Form("#gamma %d E/E_{true}",nPhoton),RooArgList(x),h1);

  RooRealVar mean("mean","peak position", xmaxbin, xmaxbin-0.1, xmaxbin+0.1, "");
  RooRealVar sigma("sigma","core #sigma",rmsh1narrow, 0.001,std::max(0.15,1.2*rmsh1narrow),"");

  //  RooRealVar Nsig("Nsig","signal yield",h1->Integral()*0.7,0.,h1->Integral()*1.1); // signal represents the peak in E/Etrue (even though it is actually only signal)
  //Nsig.setVal( h->GetSum()*0.1);
  RooRealVar Nsig("Nsig","signal yield",0.7,0.,1.0); // should use normalization
  

  RooGaussian gaus("gaus","Core Gaussian",x, mean,sigma);

  RooRealVar alphaCB  ("alphaCB","",-1,-5,-0.1);  // positive for left tail, negative for right one
  RooRealVar nCB ("nCB","",10,0.1,50);
  RooRealVar alphaCB2  ("alphaCB2","",1,0.1,5.0);  // the double CB I use requires positive parameter
  RooRealVar nCB2 ("nCB2","",10,0.1,50);
  RooCBShape cb_sig ("cb_sig","Crystal Ball",x, mean, sigma, alphaCB,nCB);
  if (useCB2toFit) {
    // the double CB I use requires positive parameter
    alphaCB.setVal(1);
    alphaCB.setRange(0.1,5.0);
  }
  My_double_CB cb2_sig = My_double_CB("cb2_sig", "cb2_sig", x, mean, sigma, alphaCB,nCB, alphaCB2,nCB2);

  RooRealVar cb0("cb0","cb0", 0.0, -5.0,5.0);
  RooRealVar cb1("cb1","cb1", 0.0, -5.,5);
  RooRealVar cb2("cb2","cb2",-1,  -5.,5.);
  RooRealVar cb3("cb3","cb3", 0.0,  -5.,5.);
  RooRealVar cb4("cb4","cb4", 0.0,  -5.,5.);
  RooRealVar cb5("cb5","cb5", 0.0,  -5.,5.);
  RooRealVar cb6("cb6","cb6", 0.0,  -5.,5.);
  //  RooRealVar cb7("cb","cb7", 0.0,  -5.,5.);

  // define a background shape in addition for the bare gaussian or Crystal Ball for the peak
  RooArgList cbpars(cb0,cb1,cb2);  
  RooArgList cbparsMore(cb0,cb1,cb2,cb3,cb4,cb5,cb6);
  RooArgList *cbparsPtr = isSecondGenPhoton ? &cbpars : &cbpars;
  RooChebychev bkg("bkg","bkg model", x, *cbparsPtr );
  //  RooRealVar Nbkg("Nbkg","background yield",h1->Integral()*0.3,0.,h1->Integral()*1.1);
  RooRealVar Nbkg("Nbkg","background yield",0.3,0.,1.0);

  //RooPolynomial bkg("bkg","background model",x,RooArgList(p0,p1,p2,p3,p4,p5,p6) );
  //RooPolynomial bkg("bkg","background model",x,RooArgList(p0,p1,p2,p3) );

  RooAbsPdf* model=0;

  RooAddPdf model1("model","sig+bkg",RooArgList(gaus,bkg),RooArgList(Nsig,Nbkg));
  RooAddPdf model2("model","sig+bkg",RooArgList(cb_sig,bkg),RooArgList(Nsig,Nbkg));
  RooAddPdf model3("model","sig+bkg",RooArgList(cb2_sig,bkg),RooArgList(Nsig,Nbkg));
  RooAddPdf model4("model","sig",RooArgList(cb2_sig),RooArgList(Nsig));
  RooAddPdf model5("model","sig",RooArgList(cb_sig),RooArgList(Nsig));
  RooAddPdf model6("model","sig",RooArgList(gaus),RooArgList(Nsig));

  if (noFitBkg) {
    if (useCB2toFit)     model = &model4;
    else if (useCBtoFit) model = &model5;
    else                 model = &model6;
  }
  else {
    if (useCB2toFit)     model = &model3;
    else if (useCBtoFit) model = &model2;
    else                 model = &model1;
  }

  RooNLLVar nll("nll","log likelihood var",*model,dh, RooFit::Extended(true), RooFit::SumW2Error(kTRUE), RooFit::Range(xlo,xhi));
  //RooNLLVar nll("nll","log likelihood var",*model,dh, RooFit::SumW2Error(kTRUE), RooFit::Range(xlo,xhi));  
  //RooAbsReal * nll = model->createNLL(dh); //suggetsed way, taht should be the same

  RooFitResult* res = nullptr;
  RooMinuit m(nll);
  RooMinimizer mfit(nll);

  // if (useCB2toFit) res = cb2_sig.fitTo(dh,RooFit::SumW2Error(kTRUE),RooFit::Save(),RooFit::Range(xlo, xhi));
  // else if (useCBtoFit) res = cb_sig.fitTo(dh,RooFit::SumW2Error(kTRUE),RooFit::Save(),RooFit::Range(xlo, xhi));
  // else            res = gaus.fitTo(dh,RooFit::SumW2Error(kTRUE),RooFit::Save(),RooFit::Range(xlo, xhi));
  if (noFitBkg) {
    if (useCB2toFit)     res = cb2_sig.fitTo(dh,RooFit::Save(),RooFit::Range(xlo, xhi),RooFit::SumW2Error(kTRUE));
    else if (useCBtoFit) res = cb_sig.fitTo(dh,RooFit::Save(),RooFit::Range(xlo, xhi),RooFit::SumW2Error(kTRUE));
    else                 res = gaus.fitTo(dh,RooFit::Save(),RooFit::Range(xlo, xhi),RooFit::SumW2Error(kTRUE));
  } else {
    
    if (useFit_RooMinuit_) {

      // // original fit
      // // obsolete: see here --> https://root-forum.cern.ch/t/roominuit-and-roominimizer-difference/18230/8
      // // better to use RooMinimizer, but please read caveat below
      m.setVerbose(kFALSE);
      //m.setVerbose(kTRUE);
      m.migrad();
      m.hesse();  // sometimes it fails, caution
      res = m.save() ;

    } else {

      // alternative fit (results are pretty much the same)
      // IMPORTANT, READ CAREFULLY: sometimes this method fails.
      // This happens because at the boundaries of the fit range the pdf goea slightly below 0 (so it is negative). The fitter tries to cope wth it and should tipically
      // manage to converge. However, I noticed that after few attemps (even though the default number of attemps should be several hundreds or thousands of times) 
      // the job crashes, and this seems to be a feature of cmssw, not of RooFit
      // The reason why the pdf gets negative could be due to the fact that, regardless the chosen fit range given by xlo and xhi, the actual fit range goes from the 
      // lower edge of the leftmost bin containing xlo to the upper edge of the rightmost one containing xhi, but then the fit tries to "pass" across the bin centers
      // Therefore, for a sharply rising (or falling) distribution, the pdf can become negative
      // The consequence is that there are large areas in the calibration map of related 2D plots that are white (because the fit there was not done succesfully)
      // The previous method using RooMinuit seems to be more robust, so I suggest we should use that one even though it is said to be obsolete
      mfit.setVerbose(kFALSE);
      mfit.setPrintLevel(-1);
      mfit.setStrategy(2);  // 0,1,2:  MINUIT strategies for dealing most efficiently with fast FCNs (0), expensive FCNs (2) and 'intermediate' FCNs (1)
      //cout << "FIT_EPSILON: Minimize" << endl;
      mfit.minimize("Minuit2","minimize");
      //cout << "FIT_EPSILON: Minimize hesse " << endl;
      mfit.minimize("Minuit2","hesse");
      //cout<<"FIT_EPSILON: Estimate minos errors for all parameters"<<endl;
      mfit.minos(RooArgSet(Nsig,Nbkg,mean));
      res = mfit.save() ;

    }

  }

  RooChi2Var chi2("chi2","chi2 var",*model,dh, true);
  // use only bins in fit range for ndof (dh is made with var x that already has the restricted range, but h is the full histogram)
  //int ndof = h->GetNbinsX() - res->floatParsFinal().getSize();
  int ndof = h1->FindFixBin(xhi) - h1->FindFixBin(xlo) + 1 - res->floatParsFinal().getSize(); 

  //compute S/B and chi2
  x.setRange("sobRange",mean.getVal() - 2.0*sigma.getVal(), mean.getVal() + 2.*sigma.getVal());
  RooAbsReal* integralSig = gaus.createIntegral(x,NormSet(x),Range("sobRange"));
  RooAbsReal* integralBkg = bkg.createIntegral(x,NormSet(x),Range("sobRange"));

  float normSig = integralSig->getVal();
  float normBkg = integralBkg->getVal();

  // here we do not have pi0, but let's be consistent with the original value (we do not really use these parameters)
  Pi0FitResult pi0res; // this is the output value of this method
  pi0res.res = res;

  pi0res.S = normSig*Nsig.getVal();
  pi0res.Serr = normSig*Nsig.getError();

  pi0res.B = normBkg*Nbkg.getVal();
  pi0res.Berr = normBkg*Nbkg.getError();

  pi0res.SoB =  pi0res.S/pi0res.B;
  pi0res.SoBerr =  pi0res.SoB*sqrt( pow(pi0res.Serr/pi0res.S,2) + 
				    pow(pi0res.Berr/pi0res.B,2) ) ;
  pi0res.dof = ndof;
  pi0res.nFitParam = res->floatParsFinal().getSize();


  RooPlot*  xframe = x.frame(h1->GetNbinsX());
  //RooPlot*  xframe = x.frame(xlo, xhi);
  xframe->SetName((nameHistofit+Form("_rp")).Data());
  xframe->SetTitle(h1->GetTitle());
  dh.plotOn(xframe, Name("data"));
  //model->plotOn(xframe,Components(bkg),LineStyle(kDashed), LineColor(kRed), RooFit::Range(xlo,xhi));
  //if (useCBtoFit and isSecondGenPhoton) model->plotOn(xframe,Components(cb_sig),LineStyle(kDashed), LineColor(kGreen+1), RooFit::Range(xlo,xhi));
  if (not noFitBkg) {
    if (useCB2toFit)     model->plotOn(xframe,Components(cb2_sig),LineStyle(kDashed), LineColor(kGreen+1), RooFit::Range(xlo,xhi));
    else if (useCBtoFit) model->plotOn(xframe,Components(cb_sig),LineStyle(kDashed), LineColor(kGreen+1), RooFit::Range(xlo,xhi));
    else                 model->plotOn(xframe,Components(gaus),LineStyle(kDashed), LineColor(kGreen+1), RooFit::Range(xlo,xhi));
  }
  model->plotOn(xframe, Name("model"),RooFit::Range(xlo,xhi));

  // TMAth::Prob() uses Chi2, not reduced Chi2, while xframe->chiSquare() returns the reduced Chi2
  pi0res.chi2 = xframe->chiSquare("model","data",pi0res.nFitParam) * pi0res.dof;
  pi0res.probchi2 = TMath::Prob(pi0res.chi2, ndof);

  xframe->Draw();

  cout << "FIT_EPSILON: "
       << "photon " << nPhoton << "  "
       << " mean " << mean.getVal() << " +/- " << mean.getError()
       << " sigma " << sigma.getVal() << " +/- " << sigma.getError()
       << " Nsig: " << Nsig.getVal() 
       << " nsig 2sig: " << normSig*Nsig.getVal()
       << " nbkg 2sig: " << normBkg*Nbkg.getVal()
       << " S/B: " << pi0res.SoB << " +/- " << pi0res.SoBerr
       << " chi2: " << pi0res.chi2
       << " chi2 reduced: " << pi0res.chi2 / pi0res.dof
       << " DOF: " << pi0res.dof
       << " N(fit.param.): " << pi0res.nFitParam
       << " prob(chi2): " << pi0res.probchi2
       << endl;

  TLatex lat;
  std::string line = "";
  lat.SetNDC();
  lat.SetTextSize(0.040);
  lat.SetTextColor(1);

  float xmin(0.15), yhi(0.8), ypass(0.05);
  if(mode==EtaEB) yhi=0.30;
  if(mode==Pi0EE) yhi=0.5;
  if(mode==Pi0EB) {
    EBDetId thisebid(EBDetId::detIdFromDenseIndex(HistoIndex) ); 
    int ieta = foldInSuperModule_ ? thisebid.ietaSM() : thisebid.ieta();
    int iphi = foldInSuperModule_ ? thisebid.iphiSM() : thisebid.iphi();
    if (foldInSuperModule_) line = Form("#gamma%d: i#eta = %d, i#phi = %d, ic() = %d", nPhoton, ieta, iphi, thisebid.ic());
    else                    line = Form("#gamma%d: i#eta = %d, i#phi = %d", nPhoton, ieta, iphi);

  } else {
    line = Form("#gamma%d", nPhoton);
  }
  lat.DrawLatex(xmin,yhi, line.c_str());

  line = Form("peak: %.2f #pm %.2f", mean.getVal(), mean.getError() );
  lat.DrawLatex(xmin,yhi-ypass, line.c_str());

  line = Form("#sigma: %.2f #pm %.2f", sigma.getVal(), sigma.getError());
  lat.DrawLatex(xmin,yhi-2.*ypass, line.c_str());

  line = Form("#Chi^{2}: %.2f (%d dof)", pi0res.chi2, pi0res.dof );
  lat.DrawLatex(xmin,yhi-3.*ypass, line.c_str());

  line = Form("fit param. %d", pi0res.nFitParam );
  lat.DrawLatex(xmin,yhi-4.*ypass, line.c_str());

  canvas->RedrawAxis("sameaxis");


  //////////////////////////////////
  //////////////////////////////////  
  
  // some parameters do not make sense for the E/Etrue study, but for simplicity we keep the same structure as the mass fit
  // basically we just need the peak position
  if (isSecondGenPhoton) {

    if(mode==Pi0EB){
      EBmap_Signal_g2[HistoIndex]=pi0res.S;
      EBmap_Backgr_g2[HistoIndex]=pi0res.B;
      EBmap_Chisqu_g2[HistoIndex]=xframe->chiSquare();
      EBmap_ndof_g2[HistoIndex]=ndof;
      EBmap_mean_g2[HistoIndex]=mean.getVal();
      EBmap_mean_err_g2[HistoIndex]=mean.getError();
      EBmap_sigma_g2[HistoIndex]=sigma.getVal();
      EBmap_Snorm_g2[HistoIndex]=normSig;
      EBmap_b0_g2[HistoIndex]=cb0.getVal();
      EBmap_b1_g2[HistoIndex]=cb1.getVal();
      EBmap_b2_g2[HistoIndex]=cb2.getVal();
      EBmap_b3_g2[HistoIndex]=cb3.getVal();
      EBmap_Bnorm_g2[HistoIndex]=normBkg;
    }
    if(mode==Pi0EE){
      EEmap_Signal_g2[HistoIndex]=pi0res.S;
      EEmap_Backgr_g2[HistoIndex]=pi0res.B;
      EEmap_Chisqu_g2[HistoIndex]=xframe->chiSquare();
      EEmap_ndof_g2[HistoIndex]=ndof;
      EEmap_mean_g2[HistoIndex]=mean.getVal();
      EEmap_mean_err_g2[HistoIndex]=mean.getError();
      EEmap_sigma_g2[HistoIndex]=sigma.getVal();
      EEmap_Snorm_g2[HistoIndex]=normSig;
      EEmap_b0_g2[HistoIndex]=cb0.getVal();
      EEmap_b1_g2[HistoIndex]=cb1.getVal();
      EEmap_b2_g2[HistoIndex]=cb2.getVal();
      EEmap_b3_g2[HistoIndex]=cb3.getVal();
      EEmap_Bnorm_g2[HistoIndex]=normBkg;
    }


  } else {

    if(mode==Pi0EB){
      EBmap_Signal[HistoIndex]=pi0res.S;
      EBmap_Backgr[HistoIndex]=pi0res.B;
      EBmap_Chisqu[HistoIndex]=xframe->chiSquare();
      EBmap_ndof[HistoIndex]=ndof;
      EBmap_mean[HistoIndex]=mean.getVal();
      EBmap_mean_err[HistoIndex]=mean.getError();
      EBmap_sigma[HistoIndex]=sigma.getVal();
      EBmap_Snorm[HistoIndex]=normSig;
      EBmap_b0[HistoIndex]=cb0.getVal();
      EBmap_b1[HistoIndex]=cb1.getVal();
      EBmap_b2[HistoIndex]=cb2.getVal();
      EBmap_b3[HistoIndex]=cb3.getVal();
      EBmap_Bnorm[HistoIndex]=normBkg;
    }
    if(mode==Pi0EE){
      EEmap_Signal[HistoIndex]=pi0res.S;
      EEmap_Backgr[HistoIndex]=pi0res.B;
      EEmap_Chisqu[HistoIndex]=xframe->chiSquare();
      EEmap_ndof[HistoIndex]=ndof;
      EEmap_mean[HistoIndex]=mean.getVal();
      EEmap_mean_err[HistoIndex]=mean.getError();
      EEmap_sigma[HistoIndex]=sigma.getVal();
      EEmap_Snorm[HistoIndex]=normSig;
      EEmap_b0[HistoIndex]=cb0.getVal();
      EEmap_b1[HistoIndex]=cb1.getVal();
      EEmap_b2[HistoIndex]=cb2.getVal();
      EEmap_b3[HistoIndex]=cb3.getVal();
      EEmap_Bnorm[HistoIndex]=normBkg;
    }

  }

  Pi0FitResult fitres = pi0res;


  // if(StoreForTest_ && niter==0){
  if(StoreForTest_){
    outfileTEST_->cd();
    xframe->Write();
    canvas->Write();
  }

  delete canvas;
  return fitres;

}





// ------------ method called once each job just before starting event loop  ------------
    void 
FitEpsilonPlot::beginJob()
{
    if(StoreForTest_){
      outfileTEST_ = new TFile(fitFileName_.c_str(),"RECREATE");
      if(!outfileTEST_) cout << "WARNING: file " << fitFileName_ << " with fit not created." << endl;
    }
}

// ------------ method called once each job just after ending the event loop  ------------
    void 
FitEpsilonPlot::endJob() 
{
  if (isEoverEtrue_) {
    // call it first with false to save first photon coefficients
    if (fitEoverEtrueWithRooFit_) {
      saveCoefficientsEoverEtrueRooFit(false);
      saveCoefficientsEoverEtrueRooFit(true);
    } else {
      saveCoefficientsEoverEtrue(false);
      saveCoefficientsEoverEtrue(true);
    }
  } else {
    saveCoefficients();
  }

  if(StoreForTest_){
    cout << "FIT_EPSILON: Fit stored in " << fitFileName_ << endl;
    outfileTEST_->Write();
    outfileTEST_->Close();
  }

}

// ------------ method called when starting to processes a run  ------------
    void 
FitEpsilonPlot::beginRun(edm::Run const&, edm::EventSetup const&)
{
}

// ------------ method called when ending the processing of a run  ------------
    void 
FitEpsilonPlot::endRun(edm::Run const&, edm::EventSetup const&)
{
}

// ------------ method called when starting to processes a luminosity block  ------------
    void 
FitEpsilonPlot::beginLuminosityBlock(edm::LuminosityBlock const&, edm::EventSetup const&)
{
}

// ------------ method called when ending the processing of a luminosity block  ------------
    void 
FitEpsilonPlot::endLuminosityBlock(edm::LuminosityBlock const&, edm::EventSetup const&)
{
}

// ------------ method fills 'descriptions' with the allowed parameters for the module  ------------
void
FitEpsilonPlot::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    //The following says we do not know what parameters are allowed so do no validation
    // Please change this to state exactly what you do use, even if it is no parameters
    edm::ParameterSetDescription desc;
    desc.setUnknown();
    descriptions.addDefault(desc);
}

//define this as a plug-in
DEFINE_FWK_MODULE(FitEpsilonPlot);
