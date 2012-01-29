/*	
	NOTE: Filters BAM alignments according to:

	1. Percentage identity (percId)
	2. Coverage (coverage)
	3. Intron gaps (noIntrons, baseInsert)
	4. Uniqueness (determined by percId and coverage)


	Created: 4-November-2011
	Last modified: 29-January-2012
*/  
 
#include <api/BamReader.h>
#include <api/BamWriter.h>
#include <api/BamAlignment.h>
#include <api/algorithms/Sort.h> 
#include "bamtools_sort.h" 
#include <iostream>
#include <vector>
#include <string>
#include <stdio.h>
#include <stdexcept> 
#include <time.h> 
#include <getopt.h> 
#include <sstream>
#include <stdlib.h>
#include <iomanip> 
#include <boost/regex.hpp>
#include <cstdio>
#include <algorithm>
#include <unordered_map> 
#include <fstream>
#include <map>
#include "header.h"


using namespace BamTools;
using namespace BamTools::Algorithms; 
using namespace std;
using namespace boost;

string bool_cast(const bool b) 
{
    ostringstream ss;
    ss << boolalpha << b;
    return ss.str();
}


struct optionalCounters_t {
  int outPaired;
  int outUniq;
  int outBest;
};



vector<string> uniqueKeys(const vector<PairednessCoverage> &m)
{
  vector<string> unKeys;

  if (m.empty()) {return unKeys;}

  // find the number of different keys
  size_t nKeys = 1;
  vector<PairednessCoverage>::const_iterator it = m.begin();
  string lastkey = it->chr;
  unKeys.push_back(it->chr);
  ++it;

  while (it != m.end()) 
	{
      if (lastkey < it->chr) 
		{
		  ++nKeys;
		  lastkey = it->chr;
		  unKeys.push_back(it->chr);
		}
	  ++it;
	}
  return unKeys;
}


void printQali(vector<BamAlignment> &qali, const RefVector &refData);
float scoreMate(BamAlignment al1, BamAlignment al2, int dist, globalOptions_t globalOptions);
void prinMatedPairsInfo(vector<BamAlignment> qali, vector<MatePairs> matepairs);
void printMatedMap(map<int,int> mated);
void processQuery(vector<BamAlignment> &qali, const RefVector &refData, globalOptions_t globalOptions, BamWriter* ptrWriter, string oldQnameStem, optionalCounters_t &optionalCounters, vector<PairednessCoverage> &pairCovSteps);
void printPairCovSteps(vector<PairednessCoverage> &pairCovSteps);
void printChrOfPairCovSteps(vector<PairednessCoverage> &pairCovSteps, string chr);
void compactifyBed(vector<PairednessCoverage> &pairCovSteps, globalOptions_t globalOptions);



int main(int argc, char *argv[])
{
  // Variable definition
  BamReader reader;
  BamWriter writer;
  BamAlignment al;
  vector<BamAlignment> qali;
  unordered_map<string,int> qNameStems;
  // unordered_map<string, int>::iterator it; /// TAKE OUT!!!
  time_t tStart, tEnd, tElapsed; 
  vector<CigarOp> cigar;
  char cigarType;
  uint32_t cigarLength;
  uint32_t qLength;
  uint32_t RefID;
  uint32_t editDistance;
  string rName;
  string qName;
  string qNameStem = ""; 
  string oldQnameStem = "";
  // Alignment
  string qSuffix;
  bool strand;
  int rstart, rend;
  int cigarSize;
  int sumMandI;
  float coverage;
  float percId;
  int32_t baseInsert;  // baseInsert = qBaseInsert+tBaseInsert, in some cases
  // Counters
  int line = 0;
  int noRefID = 0;
  int outMinId = 0;
  int outMinCover = 0;
  int outIntrons = 0;
  int outPaired = 0;
  int outUniq = 0;
  int outBest = 0;
  optionalCounters_t optionalCounters;
  // Optional counters
  optionalCounters.outPaired = outPaired;
  optionalCounters.outUniq = outUniq;
  optionalCounters.outBest = outBest;
  // Initialising options
  struct globalOptions_t globalOptions;
  vector<PairednessCoverage> pairCovSteps;  
  globalOptions = initOptions(argc, argv);
  bool best = globalOptions.best;
  bool help = globalOptions.help;
  bool noIntrons = globalOptions.noIntrons;
  bool paired = globalOptions.paired;
  bool uniq = globalOptions.uniq;
  bool verbose = globalOptions.verbose;
  int insertLimit = globalOptions.insertLimit;
  int maxIntronLen = globalOptions.maxIntronLen;
  int maxSortesTest = globalOptions.maxSortesTest;
  int minCover = globalOptions.minCover;
  int minId = globalOptions.minId;
  int minIntronLen = globalOptions.minIntronLen;
  float uniqThresh = globalOptions.uniqThresh;
  const char* commonGeneFile = globalOptions.commonGeneFile;
  const char* inputFile = globalOptions.inputFile;
  const char* outputFile = globalOptions.outputFile;	
  const char* pairBedFile = globalOptions.pairBedFile;
  ofstream geneFile;
  ofstream bedFile;

  if (verbose)
	{
	  cout << "------------------------------------------------" << endl;
	  cout << "Selected options are: " << endl;
	  cout << "best=" << best << endl;
	  cout << "help=" << help << endl;
	  cout << "noIntrons=" << noIntrons << endl;
	  cout << "paired=" << paired << endl;
	  cout << "uniq=" << uniq << endl;
	  cout << "verbose=" << verbose << endl;
	  cout << "Input file: " << inputFile << endl;
	  cout << "Output file: " << outputFile << endl;
	  cout << "insertLimit=" << insertLimit << endl;
	  cout << "maxIntronLen=" <<  maxIntronLen << endl;
	  cout << "minCover=" <<  minCover << endl;
	  cout << "minId=" <<  minId << endl;
	  cout << "minIntronLen=" << minIntronLen << endl;
	  cout << "uniqThresh=" << uniqThresh << endl;
	  cout << "commonGeneFile=" << commonGeneFile << endl;
	  cout << "pairBedFile=" << pairBedFile << endl;
	  cout << "------------------------------------------------" << endl;
	}

  // Starting timer
  tStart = time(NULL);    

  // Opening BAM file
  if (!reader.Open(inputFile)) 
	{
	cerr << "Could not open input BAM files. " << endl;
	return 0;
  	}

  // Retrieve 'metadata' from BAM files, these are required by BamWriter
  const RefVector refData = reader.GetReferenceData();
  const SamHeader header = reader.GetHeader();

  // Open BamWriter, HEADER and REFERENCE will be written into the file
  if (!writer.Open(outputFile, header, refData)) 
	{
	cerr << "Could not open output BAM file" << endl;
	return 0;
	}

  // Sweeping through alignments
  nextAlignment:
  	while (reader.GetNextAlignment(al)) 
	  {
		line++;
	   
       if (line%100000==1)
	   { 
	   	 cout << "\r processed line " << line;
       }		

		 // Call to compactify bed
		if (pairBedFile && line%10000000 == 0)
		  {
			cout << "\nCompactifying coverage after " << line << " lines..." << endl;
			// compactifyBed(pairCovSteps, globalOptions);
			cout << "done\n" << endl;
		  }


		// Update qnamestem with the current line's query name 
 		// if option 'paired' used then it expects .f,.r or /1,/2 
		// suffixes of mate pairs;
		qName = al.Name;
		qNameStem = qName; 
		RefID = al.RefID;

		if (paired)
		  {	
			// Taking out suffix: {f,r} or {1,2}
			// static const boost::regex exp("[\\.\\/]([fr12])$"); 
			// const std::string rep("");
			// qNameStem = boost::regex_replace(qNameStem, exp, rep, boost::match_default |
			// 							 boost::format_sed); 
			qNameStem = qName.substr(0, qName.find("/"));
			qSuffix = qName.substr(qName.find("/")+1, qName.length());	
		  } 

		// Filter for data whose Reference seq ID is not defined; i.e. RNAME= * in SAM format;
		// i.e. unmapped fragment without coordinate 
		rName = getReferenceName(refData, RefID);
		if (rName.find("printReferenceName")!=-1)
		  {	  
			if (verbose)
			  {
				cout << qName << " filtered out because it has no refID " << endl;
			  }
			noRefID++;
			goto nextAlignment;
		  }


		// Verifying file is sorted by query name
		// cout << line << ": oldQnamestem=" << oldQnameStem << ", qNameStem=" << qNameStem << endl;
		if (oldQnameStem.compare(qNameStem) && oldQnameStem.compare(""))   
		  { 

			// Checking whether file is sorted by query name
			if (line <= maxSortesTest && qNameStems[qNameStem])   
			  {
				cerr << "Input file not sorted by query name!\n" << qNameStem.c_str() << 
				  " occurred previously. " << endl;
				cerr << "Do either of the following: " << endl;
				cerr << "1) Convert the file into SAM with e.g. the 'bamtools' software. \n" << 
				  "   Then sort lexicographically by queryname, i.e. use the command" << endl;
				cerr << "'export LC_ALL=C' and then 'sort -k 1,1'\n" 
					 << "   Convert back again into BAM format, with e.g. 'samtools' software. " << endl;
				cerr << "2) Sort BAM file directly with your preferred software package, " << 
				  		"   e.g. 'samtools' or 'bamtools' " << endl;  	
				exit(1);
			  }	

			// if (qali.size()>0)
			//   {
			// 	processQuery(qali, refData, globalOptions, &writer, oldQnameStem, optionalCounters, pairCovSteps);
			//   }
		  }  // end outer if

  		/////////////////////////////////////////////////////////////////
  		// Filters for: "percId", "coverage" and intron gaps "noIntrons"   
  		/////////////////////////////////////////////////////////////////

  		// Fetching alignment information
  		cigar = al.CigarData;
  		cigarSize = cigar.size();
  		qName = al.Name; // query name
  		qLength = al.Length; // query length (TODO: consider situations where qLength=0,undefined)
  		RefID = al.RefID; // ID of reference seq. (later used)
  		sumMandI = 0; // Equiv to $qEnd-$qStart in PSL
  		baseInsert = 0;
  		al.GetTag("NM", editDistance); // edit distance (see SAM spec)

		// Percentage Identity filter
  		percId = 100*(qLength-editDistance)/qLength;  
  		if (percId < minId)
  	  	{
  			outMinId++;
			if (verbose)
			  {
				cout << qName << " filtered out by percid=" << percId << " < minId=" << minId << endl;
			  }
  			goto nextAlignment;
  	  	}


  		// Coverage filter
  		sumMandI = sumMandIOperations(cigar, "no");
   		coverage = (float)100*sumMandI/qLength; 
  		if (coverage < minCover)
  		  {	
  			outMinCover++;
			if (verbose)
			  {
				cout << qName << " filtered out by coverage= " << coverage << " < minCover=" <<minCover << endl;
			  }
  			goto nextAlignment;
  		  }	


  		// Intron gap filter
  		baseInsert = sumDandIOperations(cigar, "no");
  		// Save if complying with insertLimit
  		if (noIntrons && baseInsert > insertLimit)
  		  {
  			outIntrons++;
			if (verbose)
			  {
				cout << qName << " filtered out by intron criterion= " << baseInsert << " > insertLimit=" << 
					insertLimit << endl;
			  }
  			goto nextAlignment;
  		  }

	
		if (verbose)
		  {
			cout << qName << " passed with parameters (percId, coverage)=(" << percId << "," << 
			  coverage << ")"; 
			if (noIntrons)
			 { cout << ", baseInsert=" << baseInsert << " > insertLimit=" << insertLimit << endl;} 
				else {cout << endl;}; 
		  }


		// Appending coverage and percId into alignment
		std::stringstream field;
		field << percId;
		al.AddTag("pi", "Z", field.str());
 		field.str("");
		field << coverage;
		al.AddTag("co", "Z", field.str());
		field.str("");

  		// Push @qali, [$_, $targetname, $qsuffix, $strand, $tstart, $tend, $percId, $coverage]; 
        qali.push_back(al);

		oldQnameStem = qNameStem;
		if (line <= maxSortesTest)
		  {
			qNameStems[qNameStem] = 1;
		  }

	  } ////// end while 


	// /* 	Calling of: "processQuery() if ($qnamestem ne "");
	if (qNameStem.compare(""))
	  {
		processQuery(qali,refData, globalOptions, &writer, oldQnameStem, optionalCounters, pairCovSteps);
		outPaired = optionalCounters.outPaired;
		outUniq = optionalCounters.outUniq;
		outBest = optionalCounters.outBest;
		printPairCovSteps(pairCovSteps);
	  }



	// Write pairedness coverage data into pairBedFile
	if (pairBedFile)
	  {
		  bedFile.open(pairBedFile);
		  bedFile << "track type=bedGraph name=\"pairedness coverage\" description=\"pairedness coverage\"";
		  bedFile << " visibility=full color=200,100,0 altColor=200,100,0\n";
		  cout << "pairBedFile selected. Calling compactifyBed..." << endl;
		  compactifyBed(pairCovSteps, globalOptions); 

		  //  foreach my $chr (sort keys %paircovsteps)
		  // {
		  // 	my $cov = 0;
		  // 	my $pos = 0;
		  // 	next if (!@{$paircovsteps{$chr}});
		  // 	foreach my $step (@{$paircovsteps{$chr}})
		  // 	{
		  // 		print PAIRBED "$chr\t$pos\t$step->[0]\t$cov\n" if ($pos<$step->[0] && $cov>0);
		  // 		$pos = $step->[0];
		  // 		$cov += $step->[1];
		  // 	}
		  // 	warn ("inconsistent") if ($cov!=0);
		  //  }

		  bedFile.close();
	  }


	// Sorting insertlengths array
	// @insertlen = sort {$a <=> $b} @insertlen;


	// Displaying results to STDOUT
	cout <<  "\n\n------------------------------------- " << endl;
	cout <<  "\nSummary of filtered alignments: " << endl;
	cout <<  "------------------------------------- " << endl;
	cout <<  "no refID        : " << noRefID << endl;
	cout <<  "percent identity: " << outMinId << endl;
	cout <<  "coverage        : " << outMinCover << endl;
	if (noIntrons)
	  {cout <<  "nointrons       : " << outIntrons << endl;}
	if (paired) 
	  {
		cout << "not paired      : " << outPaired << endl;
      	cout << "quantiles of unspliced insert lengths: " << endl;
	  	/// Print quantiles
 	  } 
	if (uniq) 
	  {cout << "unique          : " << outUniq << endl;}
	if (best)
	  {cout << "best            : " << outBest << endl;}



	// Displaying command line
	cout <<  "------------------------------------- " << endl;
	cout << "Cmd line: " << endl;
	for (int it=0; it<argc;it++)
	  {
		cout << argv[it] << " ";
	  }
	cout << endl;
	cout <<  "------------------------------------- " << endl;

	if (verbose)
	  {
		// Printing sortedness test results
		cout << "----------------------------------------" << endl;
		cout << "File is correctly sorted " << endl;
		cout << "----------------------------------------" << endl;
	  }

	// Closing file handles
	reader.Close();
	writer.Close();

	// Ending timer and printing elapsed time
	tEnd = time(NULL);    
	tElapsed = printElapsedTime(tEnd, tStart);     

} // end main



void printQali(vector<BamAlignment> &qali, const RefVector &refData)
{
  vector<BamAlignment>::iterator it = qali.begin();
  std::stringstream ss_rstart, ss_rend, ss_percId, ss_coverage, ss_score;
  string rName;
  string qName;
  string qSuffix;
  bool strand;
  string percId, coverage, score;

  for (it; it != qali.end(); it++)
	{
	  rName = getReferenceName(refData, (*it).RefID); 
	  qName = (*it).Name; 
	  qSuffix = qName.substr(qName.find("/")+1, qName.length());	
	  strand = (*it).IsReverseStrand();
	  ss_rstart << (*it).Position; 
	  ss_rend << (*it).GetEndPosition();
  	  (*it).GetTag("pi", percId);
  	  (*it).GetTag("co", coverage);
  	  (*it).GetTag("sc", score);
 	  ss_percId << percId;  
	  ss_coverage << coverage;  
	  ss_score << score;  

	  // Printing to stdout
	  cout << "\t[ ";
	  cout << qName << " " << rName << " " << qSuffix << " " << bool_cast(strand) << " " << 
			ss_rstart.str() << " " << ss_rend.str() << " " << ss_percId.str() << " " << 
		    ss_coverage.str() << " " << ss_score.str(); 
	  cout << " ]," << endl;
	  // clear strings
	  ss_rstart.str(""); ss_rend.str(""); ss_percId.str(""); ss_coverage.str(""); ss_score.str("");
	}
}

// parameter later used for comparing quality between alignments
vector<BamAlignment> scoreAli(vector<BamAlignment>& qali)
{
  string s_percId, s_coverage;
  float percId, coverage, score;
  std::stringstream ss_score;
  vector<BamAlignment>::iterator it = qali.begin();

  for (it; it!=qali.end(); it++)
	{
	  	(*it).GetTag("pi", s_percId);
  		(*it).GetTag("co", s_coverage);
 		ss_score << atof(s_coverage.c_str())+atof(s_percId.c_str()); 
		// cout << "Adding score=" << ss_score.str() << " to " << (*it).Name << endl;
		(*it).AddTag("sc", "Z", ss_score.str());
		ss_score.str("");
	}

  return qali;

}


// for comparing quality of two mate-pair read alignments (it, jit)
float scoreMate(BamAlignment al1, BamAlignment al2, int dist, globalOptions_t globalOptions)
{
  int maxIntronLen = globalOptions.maxIntronLen;
  bool best = globalOptions.best;
  string percId1, coverage1, percId2, coverage2;
  string rf1, rf2;

  // Retrieving percent identity
  al1.GetTag("pi", percId1);
  al2.GetTag("pi", percId2);
  // Retrieving coverage values
  al1.GetTag("co", coverage1);
  al2.GetTag("co", coverage2);

  float percIdIt = atof(percId1.c_str());
  float percIdJit = atof(percId2.c_str());
  float coverageIt = atof(coverage1.c_str());
  float coverageJit = atof(coverage2.c_str());

  float score = (percIdIt + percIdJit)/100 // percent identity
				 + (coverageIt + coverageJit)/100; // percent coverage
  if (!best)
  	{ // penalty for distance between mates. Do not use if option 'best' is chosen, 
	  // otherwise a one base difference may cause a difference
	  score -= dist/maxIntronLen/10; 
	}

  return score;
}



//
// checking whether two alignments (or two alignment pairs) are similar
// Purpose: Due to separate handling of spliced and unspliced alignments it can happen
// that very similar alignments are reported, e.g. an unspliced read going approximately up to an intron
// and a spliced read with a few base pairs on one exon.
// These should not be considered ambiguous when --uniq is specified.
// TODO: check whether boolean comparisons can be made with "uint32_t"
bool similar(BamAlignment alR, BamAlignment alS, globalOptions_t globalOptions) 
{
  // Extracting options
  bool verbose = globalOptions.verbose;
	
  // First alignment
  int32_t rStart = alR.Position; 
  int32_t rEnd = alR.GetEndPosition();
  string rName = alR.Name;
  // Second alignment
  int32_t sStart = alS.Position;
  int32_t sEnd = alS.GetEndPosition();
  string sName = alS.Name;  

  if (verbose)
	{
	  cout << "[SIMILAR]: checking whether " << rName << " and " << sName << " are approx. the same:"<< endl;
	  cout << "[SIMILAR]: Overlap: (" << rName << "):" << rEnd << " <= (" << sName 
		   << "):" << sStart << " ?" << endl; 
	  cout << "[SIMILAR]: Overlap: (" << rName << "):" << rStart << " >= (" << sName 
		   << "):" << sEnd << " ?" << endl; 
	}

  if (rEnd <= sStart || rStart >= sEnd) // here: similar = overlapping target range
	{
	  cout << "[SIMILAR]: Alignments are NOT SIMILAR" << endl;
	  return false;
	} else {
		cout << "[SIMILAR]: Ans: Alignments are SIMILAR" << endl;
		return true;
  		   }
}


void printMatedPairsInfo(vector<BamAlignment> qali, vector<MatePairs> matepairs)
{
  vector<MatePairs>::iterator itMp = matepairs.begin();

  int counter = 0;
  string qName1, qName2;
  cout << "Printing Mate-pairs info: [matepairs.size()=" << matepairs.size() << "]" << endl;
  for (itMp; itMp != matepairs.end(); itMp++)
	{
	  int it = (*itMp).alIt;
	  int jit = (*itMp).alJit;
	  float score = (*itMp).score;
	  qName1 = qali.at(it).Name;
	  qName2 = qali.at(jit).Name;
	  cout << "(" << it << "," << jit << ")=" << qName1 << ", " << qName2 << ", scoreMate=" << score << endl;
 	}
}


void printMatedMap(map<int,int> mated)
{
  map<int,int>::iterator itMated = mated.begin();
  cout << "Printing mated summary:" << endl;
  for (itMated; itMated!=mated.end(); itMated++)
	{
	  cout << "mate:" << (*itMated).first << ", mated:" << (*itMated).second << " times" << endl;
	}
}


void printPairCovSteps(vector<PairednessCoverage> &pairCovSteps)
{
  vector<string> chrNames = uniqueKeys(pairCovSteps);
  string chr;

  cout << "Printing pairCovSteps contents: key=>[value(s)]" << endl;
  // Sweep through each chromosome and stuff their values into a vector 
    for (int chrIt = 0; chrIt < chrNames.size(); chrIt++ ) 
  	  {

		chr = chrNames.at(chrIt);
		cout << chr << "=>" << endl;
		int it=0;
		for (it; it<pairCovSteps.size(); it++)
		  {
			if (pairCovSteps.at(it).chr == chr)
			  cout << "\t[" << pairCovSteps.at(it).coord << "," << pairCovSteps.at(it).label << "]" << endl; 
		  }
	  }
}


void printChrOfPairCovSteps(vector<PairednessCoverage> &pairCovSteps, string chr)
{

  cout << "Printing pairCovSteps contents: key=>[value(s)]" << endl;
  // Sweep through each chromosome and stuff their values into a vector 
  cout << chr << "=>" << endl;
  int it=0;
  for (it; it<pairCovSteps.size(); it++)
	{
	  if (pairCovSteps.at(it).chr == chr)
		cout << "\t[" << pairCovSteps.at(it).coord << "," << pairCovSteps.at(it).label << "]" << endl; 
	}
}


// If several steps coincide then summarise them equivalently by one step in order to 
// 1) Save memory or 
// 2) output a bed file
void compactifyBed(vector<PairednessCoverage> & pairCovSteps, globalOptions_t globalOptions)
{
  vector<string> chrNames = uniqueKeys(pairCovSteps);
  std::sort(chrNames.begin(), chrNames.end());
  vector<string> chrCovSteps;
  string chr;
  int32_t before=0, after=0;

  // Now sweep through each chromosome and stuff into a vector all the position values corresponding to 
  // that vector
  nextChromosome:
    for (int chrIt = 0; chrIt < chrNames.size(); chrIt++ ) // display chromosomes in alph order
  	  {
		chr = chrNames.at(chrIt);
		if (chr.size() ==0) goto nextChromosome; 

		cout << "------------------------------------------------------\n";
		cout << "Pairedness coverage BEFORE sorting:\n";
		printChrOfPairCovSteps(pairCovSteps, chr);

		// Foreach "$chr" in pairCovSteps, sort the contents of the "coord" field in ascending order
        // {-1} corresponds to pEnd, and {1} corresponds to pStart
		sort(pairCovSteps.begin(), pairCovSteps.end());

		cout << "------------------------------------------------------\n";
		cout << "Pairedness coverage AFTER sorting:\n";
		printChrOfPairCovSteps(pairCovSteps, chr);
		cout << "------------------------------------------------------\n";

	  }
}



void processQuery(vector<BamAlignment> &qali, const RefVector &refData, globalOptions_t globalOptions, BamWriter* ptrWriter, string oldQnameStem, optionalCounters_t &optionalCounters, vector<PairednessCoverage> &pairCovSteps)
{
  // Optional counters
  int outPaired = optionalCounters.outPaired;
  int outUniq = optionalCounters.outUniq;
  int outBest = optionalCounters.outBest;

 
  // Expanding options
  bool best = globalOptions.best;
  bool paired = globalOptions.paired;
  bool uniq = globalOptions.uniq;
  bool verbose = globalOptions.verbose;
  int insertLimit = globalOptions.insertLimit;
  int maxIntronLen = globalOptions.maxIntronLen;
  int maxSortesTest = globalOptions.maxSortesTest;
  int minCover = globalOptions.minCover;
  int minId = globalOptions.minId;
  int minIntronLen = globalOptions.minIntronLen;
  float uniqThresh = globalOptions.uniqThresh;
  const char* commonGeneFile = globalOptions.commonGeneFile;
  const char* pairBedFile = globalOptions.pairBedFile;
  ofstream geneFile;
  ofstream bedFile;

  // Mate variables
  string itRname, jitRname;
  string itQname, jitQname;
  string itQsuffix, jitQsuffix;
  bool itStrand, jitStrand;
  // Matepairs
  vector<MatePairs> matepairs;
  MatePairs mp;
  list<int> insertlen;
  map<int,int> mated;
  int32_t inslen, dist;
  // unsigned int inslen, dist;
  int it, jit;
  uint32_t jitTstart;
  uint32_t jitTend;
  uint32_t itTstart;
  uint32_t itTend;
  string reservedField;

  if (verbose)
	{
	  cout << "----------------------------" << endl;
	  cout << "Options: (paired, uniq, best) = (" << paired << ", " << uniq << ", " << best << ")" << endl;
	  cout << "Alignments passed for processing (qali.size())= " << qali.size() << endl;
	  cout << "----------------------------" << endl;
	}


  // Defines whether to treat reads as single- or pair-ended ones
  if (paired) 
	{

	  if (verbose)
		{
		  // Printing Qali before sorting
		  cout << "------------------------------------------------------" << endl;
		  cout << "qali BEFORE sorting by Name and position:" << endl;
		  printQali(qali, refData);
		}

	  // cout << "Size of qali: " << qali.size() << ". Sorting vector\n" << endl; //////// TAKE OUT!!!!
	  // Sorting by $tname and then by $tstart
	  std::stable_sort( qali.begin(), qali.end(), Sort::ByName(Sort::AscendingOrder) );
	  std::stable_sort( qali.begin(), qali.end(), Sort::ByPosition(Sort::AscendingOrder) );
	  // cout << "Finished sorting. Continuing processing of paired alignments" << endl;  /////// TAKE OUT!!!!

	  if (verbose)
		{
		  // Printing Qali after sorting
		  cout << "------------------------------------------------------" << endl;
		  cout << "qali AFTER sorting by Name and position:" << endl;
		  printQali(qali, refData);
		  cout << "------------------------------------------------------" << endl;
		}

	  // Evaluating candidate mate-pairs.
	  // Sweeping through all possible pairings of alignments within Qali, looking 
	  // for candidates that belong to different: mate (qSuffix), strand (+,-) 
	  // and whose distance < maxInsertLength and insert length ...
	  for (it=0; it<qali.size()-1; it++)
		{
		  itRname = getReferenceName(refData, qali.at(it).RefID); 
		  itQname = qali.at(it).Name; 
		  itQsuffix = itQname.substr(itQname.find("/")+1, itQname.length());	
		  itStrand = qali.at(it).IsReverseStrand();

		  // Only loop until chromosome is different
		  for (jit=it+1; jit<qali.size() && getReferenceName(refData, qali.at(it).RefID)==
											getReferenceName(refData, qali.at(jit).RefID); jit++) 
			{
			  jitRname = getReferenceName(refData, qali.at(jit).RefID);
			  jitQname = qali.at(jit).Name;
			  jitQsuffix = jitQname.substr(jitQname.find("/")+1, jitQname.length());	
			  jitStrand = qali.at(jit).IsReverseStrand();

			  if (verbose)
				{
				  cout << "comparing pair: " << it << "," << jit << ": [" 
						<< itQname << ", " << jitQname  << "]=[" << itRname << ", mate " << itQsuffix 
					   << ", strand " << bool_cast(itStrand) << "; " << jitRname 
					   << ", mate " << jitQsuffix << ", strand " << bool_cast(jitStrand) << "]" << endl; 
				}

			  if (itQsuffix!=jitQsuffix) // different mates: (f,r) or (1,2)
  		  	  	{
  		  	  	  if (itStrand!=jitStrand) //different strands: (false, true)=(+,-)
  		  	  	  	{
		  			  jitTstart = qali.at(jit).Position; 
		  			  jitTend = qali.at(jit).GetEndPosition();//bamtools iss. with intervals taken into acct 
		  			  itTstart = qali.at(it).Position; 
    	  			  itTend = qali.at(it).GetEndPosition(); //bamtools iss. with intervals taken into acct.
  		  	  		  dist = jitTstart - itTend - 1;
  		  	  		  if (itTstart>jitTstart) {dist = itTstart - jitTend - 1;}

					  if (verbose)
						{
						  cout << "Alignments " << it << "," << jit << " above with different mates, " 
							   << "different strands, dist=" << dist 
							   << ", maxIntronLen=" << maxIntronLen << endl;
						}

					  // If alignments not too far apart but not overlapping either, then they're mated
  		  			  if (dist < maxIntronLen && dist>=0)
  		  			  	{
  		  			  	  //push @matepairs, [$i,$j,scoreMate($i,$j,$dist)];
  		  			  	  mp.setValues(it, jit, scoreMate(qali.at(it), qali.at(jit), dist, globalOptions)); 
						  matepairs.push_back(mp);

  		  			  	  if (!mated[it]) {mated[it]=0;}
  		  			  	  if (!mated[jit]) {mated[jit]=0;} 
  		  			  	  mated[it]++;
  		  			  	  mated[jit]++;
  		  			  	  inslen = jitTend - itTstart - 1;
  		  			  	  if (inslen<0) {inslen = itTend - jitTstart - 1;} 

						  if (verbose)
							{
							 cout << ">>>found mate pair:"<< it << "," << jit <<" (above alignment)!!!" << endl;
							  // cout << ": [" 
							  // 	   << itQname << ", " << jitQname  << "]=[" << itRname << ", mate " 
							  // 	   <<itQsuffix << ", strand " << bool_cast(itStrand) << "; " << jitRname 
							  // 	   << ", mate " << jitQsuffix << ", strand " << bool_cast(jitStrand) 
							  // 	   << "]" << endl; 
							  cout << "mated[" << it << "]=" << mated[it] << ",mated[" << jit <<"]="
								   << mated[jit] << ", inslen=" << inslen << ",dist=" << dist << endl;
							}

  		  			  	  // push @insertlen, $inslen;
  		  			  	  insertlen.push_back(inslen); 

  		  			  	} else {
							if (verbose)
							  {
								cout <<"dist=" << dist << " not right between " << it << " and " 
									<< jit << endl;
							  }
  		  			  } // end if(dist < maxIntronLen && dist>=0)

  		  			} // end middle if
  		  		} // end outer if

			} // end inner for
		} // end middle for


	  if (verbose)
		{
		  cout << "Summary of evaluation of candidate mate-pairs" << endl;
		  cout << "found " << matepairs.size() << " mate pairs, involving "  
			   << mated.size() << " mates." << endl;
		  cout << "------------------------------------------------------" << endl;
		  printMatedPairsInfo(qali, matepairs);
		  printMatedMap(mated);
		  cout << "------------------------------------------------------" << endl;
		  cout << "(paired,uniq,best)=" << paired << "," << uniq << "," << best << "\n";

		}

	  // Taking out all alignments that were not paired
	  int outPaired=0;
	  outPaired += (int)qali.size() - (int)mated.size();

	  if ((!uniq && !best) || matepairs.size()<2)
	  	{ // (uniq,best= (0,0) or size of mated pairs=1, let pass all mate-paired alignments
		  cout << "------------------------------------------------\n";
		  cout << "Letting pass all mated-paired alignments= " << mated.size() << ", listed below:\n";
		  cout << "Size of matepairs=" << matepairs.size() << "\n";
		  cout << "------------------------------------------------\n";

	  	  map<int,int>::iterator m_it;
	  	  for (m_it=mated.begin(); m_it!=mated.end(); m_it++)
	  		{ 
			  if (verbose)
				{cout << "Letting pass paired-alignments: (" << (*m_it).first  
					  << ") : " << qali.at((*m_it).first).Name << endl;}
			  // Saving alignments
  			  (*ptrWriter).SaveAlignment(qali.at((*m_it).first)); 
	  		}
	  	} else { // (uniq or best) selected and matepair.size()>2

   			if (verbose)
			  {
				cout << "------------------------------------------------------" << endl;
				cout << "Sort (descending) mated pairs by scoreMate" << endl;
				cout << "BEFORE sorting" << endl;
				printMatePairs(matepairs, qali);
			  }

			// Sort matepairs by score (in descending order) 	
			sort(matepairs.begin(), matepairs.end()); 
 
			if (verbose)
			  {
				cout << "AFTER sorting" << endl;
				printMatePairs(matepairs, qali);
		        cout << "------------------------------------------------------" << endl;
			  }  
 
			// Selection of Uniq-ue mate-pairs takes precedence over selecting Best mate-pairs
	  	  	if (uniq)
	  		  {// let pass only one mate pair, and only if second is significantly worse
			  	int second; float scoreFirst, scoreSecond, ratio;
				string mate1First, mate1Second, mate2First, mate2Second;
				second = 1;

		    	cout << "------------------------------------------------------" << endl;
				cout << "Comparing similarity between pairs:" << endl;
				cout << matepairs.at(0).alIt << " and " << matepairs.at(second).alIt << endl;
				cout << matepairs.at(0).alJit << " and " << matepairs.at(second).alJit << endl;
			  	cout << "------------------------------------------------------" << endl;

	  			while(second < matepairs.size() && similar(qali.at(matepairs.at(0).alIt), 
														   qali.at(matepairs.at(second).alIt), globalOptions) 
												&& similar(qali.at(matepairs.at(0).alJit), 
														   qali.at(matepairs.at(second).alJit), globalOptions)) 
	  			  {
	  				second++;
	  			  }
	  			cout << "second=" << second << endl;


				// "Second" mate-pair is the one with lowest score, but that is still similar
				if (second < matepairs.size())
				  {
					mate1First = qali.at(matepairs.at(0).alIt).Name;
					mate2First = qali.at(matepairs.at(0).alJit).Name;
					scoreFirst = matepairs.at(0).score;
					mate1Second = qali.at(matepairs.at(second).alIt).Name;
					mate2Second = qali.at(matepairs.at(second).alJit).Name;
					scoreSecond = matepairs.at(second).score;

					ratio = scoreSecond/scoreFirst;

					if (verbose)
					  {
						cout << "Selecting a unique mate-pair in terms of its score" << endl;
						cout << "------------------------------------------------------------------------" 
							<< endl;
						cout << "Position of last similar mate-pair (indexed by second)=" << second << endl;
						cout << "Comparing scores between optimal and second mate-pairs: " << endl;
						cout << "[" << mate1First << " paired with " << mate2First << "; score=" 
							 << scoreFirst << endl; 
						cout << "[" << mate1First << " paired with " << mate2Second << "; score=" 
							 << scoreSecond << endl;
						cout << "Ratio between these two mate-pairs: " << ratio << endl;
						cout << "------------------------------------------------------------------------\n"; 
					  }

					//... siginificantly worse in terms of "scoreMate"
					if (ratio < uniqThresh)
					  {
						if (verbose)
						  {
							cout << "Letting pass unique mate-pair: (" << mate1First << " and " 
								 << mate2First << ") because:" << endl;
							cout << "'lowest-scored' but similar mate-pair: (" << mate1Second << " and " 
								 << mate2Second << ")" << endl;
							cout << "is significantly worse, ratio=" << ratio << "<uniqThresh=" 
							 	<< uniqThresh << endl;
						  }
						cout << "Saving uniq mate pair (give details here)" << endl;
						(*ptrWriter).SaveAlignment(qali.at(matepairs.at(0).alIt)); 
						(*ptrWriter).SaveAlignment(qali.at(matepairs.at(0).alJit)); 
					  } else {// dropping all mate-pairs belonging to this query

					  	  if (verbose)
						    {
							  cout << "(" << matepairs.size() <<") mate-pairs filtered out by uniqueness because:";
							  cout << "\nThey are similar and have score ratio=" << ratio 
								   << ">uniqThresh=" << uniqThresh << endl;
							  for (int it=0; it<matepairs.size(); it++)
								{cout << qali.at(matepairs.at(it).alIt).Name << ", " 
									  << qali.at(matepairs.at(it).alJit).Name << ") filtered out by uniqueness " 
									  << "criterion." << endl;}	
							  cout << "Clearing contents of matepairs" << endl;				   	
							}
						  matepairs.clear();
					  }


				  } else {// '(second == matepairs.size()' => all mate-pairs in "matepairs" are similar
					  if (verbose)
						{
						  cout << "------------------------------------------------------------------------" 
								<< endl; 
				  		  cout << "Suboptimal mate-pairs are all similar" << endl;
				  	 	  cout << "Letting pass only top-scored mate-pair: " 
							   << qali.at(matepairs.at(0).alIt).Name << " and " 
							   << qali.at(matepairs.at(0).alJit).Name << ", score=" 
							   << matepairs.at(0).score << endl;
						  cout << "(" << matepairs.size()-1<< ") mate-pairs filtered out by uniqueness" << endl;
						}

					  cout << "Saving uniq mate pair (give details here)" << endl;
					  (*ptrWriter).SaveAlignment(qali.at(matepairs.at(0).alIt)); 
					  (*ptrWriter).SaveAlignment(qali.at(matepairs.at(0).alJit)); 
				}

				// keep only the best pair (if any)
				matepairs.resize(1);

	    	  } else { // (uniq, best) = (0, 1); let pass only (best) mate-pairs that share maximum score

			  	  if (verbose)
					{
					  cout << "------------------------------------------------------------------------" <<endl;
					  cout << "Options: (paired, uniq, best) = (" << paired << ", " << uniq << ", " << best 
						   << ")" << endl;
					}

				  string s_optScore, s_tempScore;
				  float optScore, tempScore; 
				  vector<string> bestTnames; 
				  optScore = matepairs.at(0).score;
				  tempScore = optScore;
				  bool qSize = true;
				  int numBest = 0;

				  while (numBest < matepairs.size() && tempScore == optScore)
					{
					
					  cout << "Letting pass mate-pair (best): " << qali.at(matepairs.at(numBest).alIt).Name 
						   << " paired with " << qali.at(matepairs.at(numBest).alJit).Name << ", score=" 
						   << tempScore << ", optScore=" << optScore << endl;
					  (*ptrWriter).SaveAlignment(qali.at(matepairs.at(numBest).alIt)); 
					  (*ptrWriter).SaveAlignment(qali.at(matepairs.at(numBest).alJit)); 
					  cout << "Storing at bestTnames=" << getReferenceName(refData, qali.at(0).RefID) << endl;
					  bestTnames.push_back(getReferenceName(refData, qali.at(numBest).RefID));
					  numBest++;
				  	  tempScore = matepairs.at(numBest).score;					  
					}
				  outBest += matepairs.size() - numBest;
				  // Retaining only mate-pairs with optimal score
				  matepairs.resize(numBest);

				if (verbose)
				  {
					cout << "------------------------------------------------------------------------" << endl;
					cout << "Filtered out alignments (best criterion): " << qali.size() << endl;
					printQali(qali, refData);		
					cout << "------------------------------------------------------------------------" << endl;
					cout << "Summary [Common target names]: " << endl;
					cout << "bestTnames.size()=" << bestTnames.size() << endl;
				  }

				  if (bestTnames.size()>1)
					{
					  unordered_map<string,int> geneNames;
					  string tName;
   					  static const boost::regex exp("\\.t\\d+");
					  const std::string rep("");

					  for (int it=0; it<bestTnames.size(); it++)
						{
						// Replace suffixes of the type chrX.t123
						tName = boost::regex_replace(bestTnames.at(it), exp, rep, boost::match_default |
														boost::format_sed); 
						  geneNames[tName]=1;
						  if (verbose) {cout << "tName= " << tName << endl;}
						}
					
					  if (geneNames.size() > 1 && commonGeneFile)
						{
						  geneFile.open(commonGeneFile);
						  unordered_map<string, int>::iterator itGn = geneNames.begin();
						  for (itGn; itGn != geneNames.end(); itGn++)
							{
							  cout << oldQnameStem << "\t"<< (*itGn).first << "\t" << (*itGn).second << endl;
							  geneFile << oldQnameStem <<"\t"<< (*itGn).first << "\t" << (*itGn).second << endl;
							}
						  geneFile.close();  
						}
					} 

			} // end if (unique)

	  	} // (!uniq && !best) || matepairs.size()<2)

	  // Write pairedness coverage infor into the pairBedFile	
	  // output pairedbed info: go through list of all mate pairs and store start and end position
	  if (pairBedFile)
		{

		  if (verbose)
			{
			  cout << "---------------------Start: Pair bed section ------------------------\n";
			  cout << "The size of matepairs=" << matepairs.size() << "\n";
			  cout << "---------------------End: Pair bed section ------------------------\n";
			}

		  int pEnd, pStart;
		  string chr;
		  PairednessCoverage pc;

		  while (matepairs.size()>0)
		  	{
		  	  chr = getReferenceName(refData, qali.at(matepairs.at(0).alIt).RefID);
		  	  pEnd = qali.at(matepairs.at(0).alJit).GetEndPosition();
		  	  pStart = qali.at(matepairs.at(0).alIt).Position-1;
			  pc.setValues(pStart, 1, chr);
			  pairCovSteps.push_back(pc);
			  pc.setValues(pEnd, -1, chr);
			  pairCovSteps.push_back(pc);
			  matepairs.erase(matepairs.begin()); // Delete first member of matepairs
		  	}
		}


	} else {// IF NOT PAIRED, single read

		if ((uniq || best) && qali.size()>1)
		  {

			// Computing scores for each alignment, score=percId+coverage;
			qali = scoreAli(qali);
			if (verbose)
			  {
				cout << "Scoring alignments and sorting them according to such score." << endl;
				cout << "------------------------------------------------------------------------" << endl;
				cout << "qali BEFORE sorting by score:" << endl;
				printQali(qali, refData);
				cout << "------------------------------------------------------------------------" << endl;
			  }

			// Sorting alignments by score
			std::stable_sort(qali.begin(), qali.end(), Sort::ByTag<std::string>("sc",Sort::DescendingOrder));

			if (verbose)
			  {
				cout << "------------------------------------------------------------------------" << endl;
				cout << "qali AFTER sorting by score:" << endl;
				printQali(qali, refData);
				cout << "------------------------------------------------------------------------" << endl;
			  }


			// Selection of Uniq-ue alignments takes precedence over selecting the Best alignments
			if (uniq) 
			  { // let pass only one alignment, and only if second is significantly worse
			  	int second; float scoreFirst, scoreSecond, ratio;
				string s_scoreFirst, s_scoreSecond;
				second = 1;

				// Sweep through all alignments and stop until a di-similar one is found
				while (second < qali.size() && similar(qali.at(0), qali.at(second), globalOptions))
				  {
					second++;
				  }


				// "Second" alignment is the one with lowest score, but that is still similar
				if (second < qali.size())
				  {

					// Comparing scores between best-scored alignment and one indexed by "second"
					qali.at(0).GetTag("sc", s_scoreFirst);
					scoreFirst = atof(s_scoreFirst.c_str());
					qali.at(second).GetTag("sc", s_scoreSecond);
					scoreSecond = atof(s_scoreSecond.c_str());
					ratio = scoreSecond/scoreFirst;

					if (verbose)
					  {
						cout << "Selecting a unique alignment in terms of its score" << endl;
						cout << "------------------------------------------------------------------------" 
							<< endl;
						cout << "Position of second worst alignment (index)=" << second << endl;
						cout << "Comparing scores between optimal and second: " << endl;
						cout << "[" << getReferenceName(refData, qali.at(0).RefID) << "]<-" 
							 << qali.at(0).Name << "; score=" << scoreFirst << " and " << endl; 
						cout << "[" << getReferenceName(refData, qali.at(second).RefID) << "]<-" 
							<< qali.at(second).Name << "; score=" << scoreSecond << endl;
						cout << "Ratio between these two alignments: " << ratio << endl;
						cout << "------------------------------------------------------------------------\n"; 
					  }
	
					// ... significantly worse in terms of "scoreAli"
					if (ratio < uniqThresh) 
					  {		
						if (verbose)
						  {
							cout << "Letting pass unique alignment " << qali.at(0).Name << " because:" << endl;
							cout << "'lowest-scored' but similar alignment " << qali.at(second).Name << endl;
							cout << "is significantly worse, ratio=" << ratio << "<uniqThresh=" 
							 	<< uniqThresh << endl;
						  }
						// // Removing percId and coverage Tags before writing into file
						// qali.at(0).RemoveTag("pi"); qali.at(0).RemoveTag("co");
						(*ptrWriter).SaveAlignment(qali.at(0)); // Prints alignment line into file
						outUniq += qali.size()-1;
					  	cout << "(" << outUniq << "/" << qali.size() << ") alignments filtered out " 
							 << " by uniqueness " << endl;
							for (int it=0; it<qali.size(); it++)
							  {cout << qali.at(it).Name << " filtered out by uniqueness criterion." << endl;}
					  } else { // dropping all alignments belonging to the same query
					  	
					  	outUniq += qali.size(); // Filtered alignments by uniqueness increases by size of Qali
						if (verbose)
						  {
							cout << "(" << qali.size() << ") alignments filtered out by uniqueness because:";
							cout << "\nratio=" << ratio << ">uniqThresh=" << uniqThresh 
								 << " between top and lowest-scored similar alignments." << endl;
							for (int it=0; it<qali.size(); it++)
							  {cout << qali.at(it).Name << " filtered out by uniqueness criterion." << endl;}
						  }					  	
					  }
				  } else {// '(second == qali.size()' => all alignments in "qali" are similar
				  	  if (verbose)
						{	
	       	              qali.at(0).GetTag("sc", s_scoreFirst);
						  scoreFirst = atof(s_scoreFirst.c_str());
						  cout << "------------------------------------------------------------------------" 
							<< endl;
				  	 	  cout << "Suboptimal alignments are all similar " << endl;
				  	 	  cout << "Letting pass only top-scored alignment: " << qali.at(0).Name 
							   << ", score=" << scoreFirst << endl;
						  cout << "(" << qali.size()-1 << ") alignments filtered out by uniqueness" << endl;
						  for (int it=1; it<qali.size(); it++)
						 	{cout << qali.at(it).Name << " filtered out by uniqueness criterion." << endl;}
						  cout << "------------------------------------------------------------------------" 
							<< endl;
						}
					  // // Removing percId and coverage Tags before writing into file
					  // qali.at(0).RemoveTag("pi"); qali.at(0).RemoveTag("co");
				  	  (*ptrWriter).SaveAlignment(qali.at(0)); // Letting pass only best
				  	  outUniq += qali.size()-1;
				  }

			  } else { // (uniq, best) = (0, 1); let pass only (best) alignments that share maximum score

			  if (verbose)
				{
				  cout << "------------------------------------------------------------------------" << endl;
				  cout << "Options: (paired, uniq, best) = (" << paired << ", " << uniq << ", " << best 
					<< ")" << endl;
				}

			  	string s_optScore, s_tempScore;
			  	float optScore, tempScore; 
				vector<string> bestTnames; 
				qali.at(0).GetTag("sc", s_optScore);
				optScore = atof(s_optScore.c_str());
				tempScore = optScore;
				bool qSize = true;

				while (tempScore == optScore && qSize)
				  {
					if (verbose)
					  {
						cout << "Letting pass alignment (best): " << qali.at(0).Name << ", score=" 
							 << tempScore << ", optScore=" << optScore << endl;
						cout << "Storing at bestTnames=" << getReferenceName(refData, qali.at(0).RefID) << endl;
					  }

					// // Removing percId and coverage Tags before writing into file
					// qali.at(0).RemoveTag("pi"); qali.at(0).RemoveTag("co");
					(*ptrWriter).SaveAlignment(qali.at(0)); 
					bestTnames.push_back(getReferenceName(refData, qali.at(0).RefID));
					qali.erase(qali.begin()); // Delete first member of qali
					if (verbose)
					  {cout << "After deleting element, qali.size()=" << qali.size() << endl;}
					if (qali.size()>0)
					  {
						qali.at(0).GetTag("sc", s_tempScore);
						tempScore = atof(s_tempScore.c_str());
					  } else {
					  	  qSize = false;
					  }
						
				  }

				outBest += qali.size();
				if (verbose)
				  {
					cout << "------------------------------------------------------------------------" << endl;
					cout << "Filtered out alignments (best criterion): " << qali.size() << endl;
					printQali(qali, refData);		
					cout << "------------------------------------------------------------------------" << endl;
					cout << "Summary [Common target names]: " << endl;
					cout << "bestTnames.size()=" << bestTnames.size() << endl;
				  }

				if (bestTnames.size()>1)
				  {
					unordered_map<string,int> geneNames;
					string tName;
					static const boost::regex exp("\\.t\\d+");
					const std::string rep("");

					for (int it=0; it<bestTnames.size(); it++)
					  {
						// Replace suffixes of the type chrX.t123
						tName = boost::regex_replace(bestTnames.at(it), exp, rep, boost::match_default |
														boost::format_sed); 
						geneNames[tName]=1;
						if (verbose) {cout << "tName= " << tName << endl;}
					  }
					
					if (verbose) {cout << "Size of geneNames=" << geneNames.size() << endl;}
					if (geneNames.size() > 1 && commonGeneFile)
					  {
						geneFile.open(commonGeneFile);
						unordered_map<string,int>::iterator itGn = geneNames.begin();

						// Writing name of query first..
						geneFile << oldQnameStem;

						// Then writing the common genes for such query
						for (itGn; itGn != geneNames.end(); itGn++)
						  {
							if (verbose) 
							  {
								cout << "commonGeneFile:" << oldQnameStem << "\t"<< (*itGn).first << "\t" 
									<< (*itGn).second << endl;
							  }
							 geneFile << "\t"<< (*itGn).first;
						  }
						geneFile << endl;
						geneFile.close();
					  }
				  } 

			  } // end if (unique)	

		} else { // (uniq, best) = (0, 0) && qali.size()>1)"

		  if (verbose)
			{
		  		cout << "Letting pass alignments, i.e. a total of: " << qali.size() << endl;
			}

			for(int it=0; it<qali.size(); it++)
				{
				  // // Removing percId and coverage Tags before writing into file
				  // qali.at(it).RemoveTag("pi"); qali.at(it).RemoveTag("co");
				  (*ptrWriter).SaveAlignment(qali.at(it)); // Prints alignment line into file
				}

		} // end if !(uniq || paired)


  } // end if !(paired)


  // Clearing contents of qali
  qali.clear();

  // Updating filter counters	
  optionalCounters.outPaired = outPaired;
  optionalCounters.outUniq = outUniq;
  optionalCounters.outBest = outBest;

} // end processQuery()
