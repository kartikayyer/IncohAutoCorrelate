#include "Statistics.h"
#include <omp.h>

#include "ArrayOperators.h"
#include "ProfileTime.h"


namespace Statistics
{
	void Get_OrientationSphere(float *& Vectors, std::vector<Settings::HitEvent> EventList)
	{
		unsigned int Size = EventList.size();
		Vectors = new float[3 * Size]();


		for (unsigned int i = 0; i < Size; i++)
		{
			float* t_Vec = new float[3]();
			t_Vec[0] = 1;
			t_Vec[1] = 0;
			t_Vec[2] = 0;


			ArrayOperators::Rotate(t_Vec, EventList[i].RotMatrix);


			Vectors[3 * i + 0] = t_Vec[0];
			Vectors[3 * i + 1] = t_Vec[1];
			Vectors[3 * i + 2] = t_Vec[2];
		}


	}
	Histogram Make_AllPixel_Histogram(Settings & Options, Detector & RefDet, unsigned int Bins, double SmallestVal, double HighestVal)
	{
		Histogram Hist(Bins, (HighestVal - SmallestVal) / Bins, SmallestVal);
		Detector Det(RefDet, true);

		ProfileTime profiler;
		profiler.Tic();

		double CounterStep = Options.HitEvents.size() / 100.0;
		int Prog = 0;

		for (unsigned int i = 0; i < Options.HitEvents.size(); i++)
		{
			Det.LoadIntensityData(&Options.HitEvents[i]);
			if (Det.Checklist.PixelMask)
				Det.ApplyPixelMask();

			for (int j = 0; j < Det.DetectorSize[0]* Det.DetectorSize[1]; j++)
			{
				Hist.AddValue(Det.Intensity[j]);
			}
			if (Options.echo)
			{
				if (i / CounterStep > Prog)
				{
					std::cout << "Pattern " << i << " / " << Options.HitEvents.size() << " \t^= " << Prog << "%\n";
					profiler.Toc(true);
					Prog++;
				}
			}
		}
		profiler.Toc(Options.echo);
		return Hist;
	}

	std::vector<Histogram> MakePixelHistogramStack(Settings & Options, Detector & RefDet, unsigned int Bins, double SmallestVal, double HighestVal)
	{
		std::vector<Histogram> HistStack;
		HistStack.reserve(RefDet.DetectorSize[0] * RefDet.DetectorSize[1]);
		//create empty Histograms:
		for (unsigned int i = 0; i < RefDet.DetectorSize[0]*RefDet.DetectorSize[1]; i++)
		{
			Histogram Hist(Bins, (HighestVal - SmallestVal) / Bins, SmallestVal);
			HistStack.push_back(Hist);
		}
		Detector Det(RefDet, true);

		ProfileTime profiler;
		profiler.Tic();

		double CounterStep = Options.HitEvents.size() / 100.0;
		int Prog = 0;

		for (unsigned int i = 0; i < Options.HitEvents.size(); i++)
		{

			Det.LoadIntensityData(&Options.HitEvents[i]);
			if (Det.Checklist.PixelMask)
				Det.ApplyPixelMask();

			#pragma omp parallel for
			for (int j = 0; j < Det.DetectorSize[0] * Det.DetectorSize[1]; j++)
			{
				HistStack[j].AddValue(Det.Intensity[j]);
			}

			if (Options.echo)
			{
				if (i / CounterStep > Prog)
				{
					std::cout << "Pattern " << i << " / " << Options.HitEvents.size() << " \t^= " << Prog << "%\n";
					profiler.Toc(true);
					Prog++;
				}
			}

		}
		profiler.Toc(Options.echo);

		return HistStack;
	}

	void CreateAndSaveAllPixelHistograms(Create_PixelHistogramSettings HistSettings, Detector & RefDet, Settings & Options)
	{
		std::vector<Statistics::Histogram> HistStack = Statistics::MakePixelHistogramStack(Options, RefDet, HistSettings.Bins, HistSettings.SmalestValue, HistSettings.LargestValue);
		
		double * FinalHistStack = new double[HistSettings.Bins * RefDet.DetectorSize[0] * RefDet.DetectorSize[1]]();
	
		for (unsigned int i = 0; i < RefDet.DetectorSize[0] * RefDet.DetectorSize[1]; i++)
		{
			unsigned long AllBinCount = 0;
			for (unsigned int j = 0; j < HistSettings.Bins; j++) //sum up all Bins (without over and underflow)
			{
				AllBinCount += HistStack[i].HistogramContent[j];
			}
			if (AllBinCount > 0)
			{
				for (unsigned int j = 0; j < HistSettings.Bins; j++) //normalize
				{
					if (HistSettings.Normalized)
					{
						FinalHistStack[HistSettings.Bins*i + j] = ((double)HistStack[i].HistogramContent[j] / ((double)AllBinCount));
					}
					else
					{
						FinalHistStack[HistSettings.Bins*i + j] = (double)HistStack[i].HistogramContent[j];
					}
				}
			}
		}

		ArrayOperators::SafeArrayToFile(HistSettings.OutputPath, FinalHistStack, HistSettings.Bins * RefDet.DetectorSize[0] * RefDet.DetectorSize[1], ArrayOperators::Binary);

		delete[] FinalHistStack;
	}


	double GetAverageRatioOfPixelsWithHits(Settings & Options, Detector & RefDet, float Offset, unsigned int LowerBound, unsigned int UpperBound, double & StDev)
	{
		if (UpperBound - LowerBound < 1)
			return 0.0;

		std::vector<double> MeanVal;
		MeanVal.resize(UpperBound - LowerBound);

		for (unsigned int ind = 0; ind < UpperBound - LowerBound; ind++)
		{
			RefDet.LoadIntensityData(&Options.HitEvents[ind + LowerBound]);

			//Count Pixel with value >= Offset
			unsigned int Counter = 0;
			for (int i = 0; i < RefDet.DetectorSize[0] * RefDet.DetectorSize[1]; i++)
			{
				if (RefDet.Intensity[i] >= Offset)
					Counter++;
			}

			//Get ratio
			MeanVal[ind] = ((double)Counter) / ((double)(RefDet.DetectorSize[0] * RefDet.DetectorSize[1]));
		}




		return 0.0;
	}
	double GetAverageRatioOfPixelsWithHits(Settings & Options, Detector & RefDet, float Offset, unsigned int LowerBound, unsigned int UpperBound)
	{
		double temp;
		return GetAverageRatioOfPixelsWithHits(Options, RefDet, Offset, LowerBound, UpperBound, *&temp);
	}
	double GetAverageRatioOfPixelsWithHits(Settings & Options, Detector & RefDet, float Offset, double & StDev)
	{
		return GetAverageRatioOfPixelsWithHits(Options, RefDet, Offset, 0, Options.HitEvents.size(), StDev);
	}
	double GetAverageRatioOfPixelsWithHits(Settings & Options, Detector & RefDet, float Offset)
	{
		double temp;
		return GetAverageRatioOfPixelsWithHits(Options,RefDet,Offset,0,Options.HitEvents.size(), *&temp);
	}

	SpeckleContrastStatistics GetSpeckleContrastStatistics(Settings & Options, Detector & RefDet, unsigned int LowerBound, unsigned int UpperBound, float Offset, float Step)
	{
		SpeckleContrastStatistics SCS;

		//Get number of masked pixel
		SCS.NumberOfUnmaskedPixels = 0;
		for (unsigned int i = 0; i < RefDet.DetectorSize[0] * RefDet.DetectorSize[1]; i++)
		{
			SCS.NumberOfUnmaskedPixels += RefDet.PixelMask[i];
		}

		//Preallocate outer vector
		SCS.SCC_Statistics.resize(UpperBound - LowerBound);

		//Loop through patterns --- can be parallized therefore clone det, ...
		#pragma omp parallel for
		for (unsigned int i = LowerBound; i < UpperBound; i++)
		{
			//Load Intensity
			Detector Det(RefDet, true);
			#pragma omp critical
			{
				Det.LoadIntensityData(&Options.HitEvents[i]);
			}
			Det.ApplyPixelMask();
			//Loop through Pixel
			ArrayOperators::DiscretizeToPhotons(Det.Intensity, Offset, Step, Det.DetectorSize[0] * Det.DetectorSize[1]);
			for (unsigned int j = 0; j < Det.DetectorSize[0] * Det.DetectorSize[1]; j++)
			{
				unsigned int Photons = Det.Intensity[j];
				if (Photons == 0)
					continue;
				//Check if propability-vector is large enought and expand if needed
				while (Photons > SCS.SCC_Statistics[i].Probability.size())
				{
					SCS.SCC_Statistics[i].Probability.push_back(0);
					if (Photons > SCS.Nmax)
						SCS.Nmax = Photons; // store maximum of photons found per pixel
				}
				//Adds entry to photon-count histogram
				SCS.SCC_Statistics[i].Probability[Photons - 1] += 1.0/((double)SCS.NumberOfUnmaskedPixels);
				//updates mean number of photons
				SCS.SCC_Statistics[i].MeanPhotonDensity += (double)Photons / ((double)SCS.NumberOfUnmaskedPixels);
			}

		}

		return SCS;
	}





	Histogram::Histogram(unsigned int size, double binSize, double firstBin)
	{
		Size = size;
		BinSize = binSize;
		FirstBin = firstBin;

		HistogramContent.clear();
		HistogramContent.resize(Size, 0);

		Entries = 0;
	}
	void Histogram::AddValue(double Value)
	{
		#pragma omp critical
		Entries ++;
		Value = Value - FirstBin;
		if (Value < 0)
		{
			#pragma omp critical
			UnderflowBin++;
			return;
		}
		unsigned int ind = (unsigned int)floor((Value / BinSize) + 0.5);
		if (ind >= Size)
		{
			#pragma omp critical
			OverflowBin++;
			return;
		}
		#pragma omp critical
		HistogramContent[ind] ++;
	}
	void Histogram::SafeToFile(std::string Filename)
	{
		double * Hist = new double[Size + 2]();

		Hist[0]     = (double)UnderflowBin;
		Hist[Size + 1] = (double)OverflowBin;

		for (unsigned int i = 0; i < HistogramContent.size(); i++)
		{
			Hist[i+1] = (double)HistogramContent[i];
		}
		
		ArrayOperators::SafeArrayToFile(Filename, Hist, Size + 2, ArrayOperators::Binary);

		delete[] Hist;
	}
}