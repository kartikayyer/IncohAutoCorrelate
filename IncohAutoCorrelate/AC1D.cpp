#include "AC1D.h"
#include <thread>
#include <vector>
#include "ArrayOperators.h"
#include "ProfileTime.h"
#include <mutex>
#include <functional>
#include <unordered_map>

AC1D::AC1D()
{
	//Needed for destructor to not crash if one of this isn't needed
	//Yes its waste of memory, ... think of a better way ...
	//CQ = new double[1];
	//AC_UW = new double[1];
	//AC = new double[1];
	//Q = new double[1];
}


AC1D::~AC1D()
{
	//delete[] CQ;
	//delete[] AC_UW;
	//delete[] AC;
	//delete[] Q;
}

void AC1D::Initialize()
{
	delete[] CQ;
	delete[] AC_UW;
	delete[] AC;
	delete[] Q;

	CQ = new double[Shape.Size]();
	AC_UW = new double[Shape.Size]();
	AC = new double[Shape.Size]();
	Q = new double[Shape.Size]();

	for (unsigned int i = 0; i < Shape.Size; i++)
	{
		Q[i] = (float)i * Shape.dq_per_Step;
	}
}
void AC1D::Initialize(Detector & Det, unsigned int ArraySize)
{
	Shape.Size = ArraySize;
	Shape.Max_Q = (float)(sqrt(Det.Max_q[0] * Det.Max_q[0] + Det.Max_q[1] * Det.Max_q[1] + Det.Max_q[2] * Det.Max_q[2]));
	Shape.dq_per_Step = Shape.Max_Q / ((float)(Shape.Size + 1));
	Initialize();
}
void AC1D::Initialize(Detector & Det, unsigned int ArraySize, float QZoom)
{
	Shape.Size = ArraySize;
	Shape.Max_Q = (float)(sqrt(Det.Max_q[0] * Det.Max_q[0] + Det.Max_q[1] * Det.Max_q[1] + Det.Max_q[2] * Det.Max_q[2]))/QZoom ;
	Shape.dq_per_Step = Shape.Max_Q / ((float)(Shape.Size + 1));
	Initialize();
}

void AC1D::Calculate_CQ(Detector & Det, Settings & Options, Settings::Interpolation IterpolMode) //Det need avIntensity already be loaded
{
	//profiler stuff
	ProfileTime Profiler;
	//reserve OpenCL Device
	int OpenCLDeviceNumber = -1;
	cl_int err;

	while ((OpenCLDeviceNumber = Options.OCL_ReserveDevice()) == -1)
	{
		std::this_thread::sleep_for(std::chrono::microseconds(Options.ThreadSleepForOCLDev));
	}

	//obtain Device
	cl::Device CL_Device = Options.CL_devices[OpenCLDeviceNumber];


	double Multiplicator = 1e-10;

	float Min_I = 0, Max_I = 0, Mean_I = 0;
	ArrayOperators::Min_Max_Mean_Value(Det.Intensity, Det.DetectorSize[0] * Det.DetectorSize[1], Min_I, Max_I, Mean_I);

	for (; 1 > Mean_I*Mean_I * Multiplicator; )
	{
		Multiplicator *= 10;
	}
	Multiplicator = Multiplicator * 1e5;

	if (Multiplicator > 1e18)
		Multiplicator = 1e18;
	



	unsigned int MapAndReduce_Factor = 10000; //Take care of Device Memory, shouldn't be a problem because its small in comp. to 3d case
	unsigned int TempArraySize = MapAndReduce_Factor * Shape.Size;

	double Params[8];
	Params[0] = (double)(Det.DetectorSize[0] * Det.DetectorSize[1]);
	Params[1] = (double)Shape.dq_per_Step;
	Params[2] = (double)Shape.Size;
	Params[3] = (double)IterpolMode;
	Params[4] = (double)Shape.Max_Q;
	Params[5] = (double)Multiplicator;
	Params[6] = (double)MapAndReduce_Factor; //Map and reduce: sub sections
	Params[7] = (double)Options.echo;
	

	if (EchoLevel > 1)
	{ //Print Parameter
		std::cout << "Parameter for 1D C(q):\n";
		for (int i = 0; i < 7; i++)
		{
			std::cout << Params[i] << "\n";
		}
	}


	uint64_t * TempArray = new uint64_t[TempArraySize]();

	if (EchoLevel > 2)
		std::cout << "TempArraySize: " << TempArraySize << "\n";

	//Setup Queue
	cl::CommandQueue queue(Options.CL_context, CL_Device, 0, &err);
	Options.checkErr(err, "Setup CommandQueue in AC1D::Calculate_CQ() ");
	cl::Event cl_event;


	//Define Kernel Buffers
	//Output
	size_t ACsize = sizeof(uint64_t) * TempArraySize;
	cl::Buffer CL_CQ(Options.CL_context, CL_MEM_WRITE_ONLY | CL_MEM_COPY_HOST_PTR, ACsize, TempArray, &err);
	//Input:
	size_t Intsize = sizeof(float) * Det.DetectorSize[0] * Det.DetectorSize[1];
	cl::Buffer CL_Intensity(Options.CL_context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, Intsize, Det.Intensity, &err);
	size_t KMapsize = sizeof(float) * 3 * Det.DetectorSize[0] * Det.DetectorSize[1];
	cl::Buffer CL_kMap(Options.CL_context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, KMapsize, Det.kMap, &err);
	cl::Buffer CL_Params(Options.CL_context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(Params), &Params, &err);

	if (EchoLevel > 2)
	{
		std::cout << "Memory needed by OCl kernel: " << ((ACsize + Intsize + KMapsize + sizeof(Params))/(1024.0*1024.0)) << "MB\n";
	}

	//Setup Kernel
	cl::Kernel kernel(Options.CL_Program, "AutoCorr_CQ_AV", &err);
	Options.checkErr(err, "Setup AutoCorr_CQ in AC1D::Calculate_CQ() ");

	//Set Arguments
	kernel.setArg(0, CL_Intensity);
	kernel.setArg(1, CL_kMap);
	kernel.setArg(2, CL_Params);
	kernel.setArg(3, CL_CQ);
	const size_t &global_size = Det.DetectorSize[0] * Det.DetectorSize[1];

	//launch Kernel
	if (Options.echo)
		Options.Echo("Launch kernel ... \n");
	Profiler.Tic();
	err = queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(global_size), cl::NullRange, NULL, &cl_event);

	Options.checkErr(err, "Launch Kernel in AC1D::Calculate_CQ() ");
	cl_event.wait();

	if (EchoLevel > 0)
	{
		std::cout << "C(q)-AV kernel finished in\n";
		Profiler.Toc(true);
	}

	err = queue.enqueueReadBuffer(CL_CQ, CL_TRUE, 0, ACsize, TempArray);
	Options.checkErr(err, "OpenCL kernel, launched in AC1D::Calculate_CQ() ");


	
	//Free Device
	Options.OCL_FreeDevice(OpenCLDeviceNumber);

	//convert to Double and Reduce
	delete[] CQ;
	CQ = new double[Shape.Size]();

	int j = 0;
	for (unsigned int i = 0; i < TempArraySize; i++)
	{
		if (j == Shape.Size)
			j = 0;

		CQ[j] += ((double)TempArray[i] / (double)Multiplicator);
		j++;
	}
	//Free memory
	delete[] TempArray;
}

// <AC_UW>
// <AC_UW sparse>
std::mutex g_loadFile_mutex;
std::mutex g_echo_mutex;
void Calculate_AC_UW_Mapped(Settings & Options,Detector & RefDet, double * AC_M, unsigned int LowerBound, unsigned int UpperBound, Settings::Interpolation IterpolMode, std::array<float, 2> Photonisation, float MaxQ, float dqdx)
{
	//g_echo_mutex.lock();
	//std::cout << "Thread from " << LowerBound <<" launched\n";
	//g_echo_mutex.unlock();

	Detector Det(RefDet, true);
	delete[] Det.Intensity;
	//if (JungfrauDet)
	//{
	//	Det.Intensity = new float[1];
	//}
	//else
	//{
		Det.Intensity = new float[Det.DetectorSize[0]* Det.DetectorSize[1]]();
	/*}*/

	for (unsigned int i = LowerBound; i < UpperBound; i++)
	{
		if (Options.echo)
		{
			if ((i - LowerBound) % 100 == 0 && i > LowerBound)
			{
				g_echo_mutex.lock();
				std::cout << "AC (uw)-Thread " << LowerBound << " - " << UpperBound << " : " << i - LowerBound << "/" << (UpperBound - LowerBound) << std::endl;
				g_echo_mutex.unlock();
			}
		}

		//Load Intensity
		g_loadFile_mutex.lock();
		//if (JungfrauDet)
		//{
		//	Det.LoadIntensityData_PSANA_StyleJungfr(Options.HitEvents[i].Filename, Options.HitEvents[i].Dataset, Options.HitEvents[i].Event);
		//}
		//else
		//{
			delete[] Det.Intensity;
			Det.Intensity = new float[Det.DetectorSize[0] * Det.DetectorSize[1]]();
			Det.GetSliceOutOfHDFCuboid(Det.Intensity, Options.HitEvents[i].Filename, Options.HitEvents[i].Dataset, Options.HitEvents[i].Event);
		//}
		g_loadFile_mutex.unlock();

		ArrayOperators::MultiplyElementwise(Det.Intensity, Det.PixelMask, Det.DetectorSize[0] * Det.DetectorSize[1]);

		//Create sparse List (and Photonize)
		Det.SparseHitList.clear();
		Det.CreateSparseHitList(Photonisation[0], Photonisation[1],false); //suppress OMP parallelization within Threadpool!
		//std::cout << "Sparse List Size: " << Det.SparseHitList.size() << "\n";
		//iterate over all combinations

		float SHLsizeQuot = ((float)Det.SparseHitList.size()) / ((float)(Det.DetectorSize[0] * Det.DetectorSize[1]));
		if (SHLsizeQuot < 0.0075f) // (switch for SparseHitList.size / DetSize > p(0.0075))
		{//CPU Mode
			//std::cout << "CPU MODE\n";
			for (unsigned int j = 0; j < Det.SparseHitList.size(); j++)
			{
				for (unsigned int k = j; k < Det.SparseHitList.size(); k++)
				{
					float q = sqrt(
						(Det.SparseHitList[j][0] - Det.SparseHitList[k][0]) * (Det.SparseHitList[j][0] - Det.SparseHitList[k][0]) +
						(Det.SparseHitList[j][1] - Det.SparseHitList[k][1]) * (Det.SparseHitList[j][1] - Det.SparseHitList[k][1]) +
						(Det.SparseHitList[j][2] - Det.SparseHitList[k][2]) * (Det.SparseHitList[j][2] - Det.SparseHitList[k][2]));

					if (q > MaxQ)
					{
						continue;
					}
					q = q / dqdx;

					if (IterpolMode == Settings::Interpolation::NearestNeighbour)
					{
						unsigned int sc;
						sc = (unsigned int)(floor(q + 0.5));
						AC_M[sc] += 2.0f * (Det.SparseHitList[j][3] * Det.SparseHitList[k][3]); //factor 2 because of both orientations (k = j; ...)
					}
					else if (IterpolMode == Settings::Interpolation::Linear)
					{
						unsigned int sc1, sc2;
						sc1 = (unsigned int)(floor(q));
						sc2 = sc1 + 1;

						float Sep = q - sc1; //separator

						float Val1 = 2.0f * ((Det.SparseHitList[j][3] * Det.SparseHitList[k][3]) * (1 - Sep));
						float Val2 = 2.0f * ((Det.SparseHitList[j][3] * Det.SparseHitList[k][3]) * (Sep));

						AC_M[sc1] += Val1;
						AC_M[sc2] += Val2;
					}
				}
			}
		}
		else //GPU Mode
		{
			//std::cout << "GPU MODE\n";
			double Multiplicator = 100; //1 is sufficient for photon discretised values (only integer possible, nearest neighbour), 100 should be good for linear interpol.

			int MapAndReduce = 10000;
			int VecSize = (int)ceilf(MaxQ / dqdx) - 1; //the -1 compensates for zeropadding
			int TempArraySize = MapAndReduce * VecSize;

			//set Parameter
			double Params[7];

			Params[0] = (double)Det.SparseHitList.size();
			Params[1] = (double)VecSize;
			Params[2] = (double)MaxQ;
			Params[3] = (double)dqdx;
			Params[4] = Multiplicator; //Multiplicator for conversion to long
			Params[5] = (double)MapAndReduce;
			Params[6] = (double)IterpolMode; //Not implementet, only nearest neighbours
			
			uint64_t * TempArray = new uint64_t[TempArraySize]();
			
			//Setup OpenCL stuff and reserve decvice
			int OpenCLDeviceNumber = -1;
			cl_int err;
			while ((OpenCLDeviceNumber = Options.OCL_ReserveDevice()) == -1)//reserve OpenCL Device
			{
				std::this_thread::sleep_for(std::chrono::microseconds(Options.ThreadSleepForOCLDev));
			}


			//obtain Device
			cl::Device CL_Device = Options.CL_devices[OpenCLDeviceNumber];

			//Setup Queue
			cl::CommandQueue queue(Options.CL_context, CL_Device, 0, &err);
			Options.checkErr(err, "Setup CommandQueue in AC1D::Calculate_AC_UW_Mapped() ");
			cl::Event cl_event;

			//Input Buffer:
			size_t SparseListSize = sizeof(float) * 4 * Det.SparseHitList.size();
			cl::Buffer CL_SparseList(Options.CL_context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, SparseListSize, (void*)Det.SparseHitList.data(), &err);
			cl::Buffer CL_Params(Options.CL_context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(Params), &Params, &err);

			//Output Buffer
			size_t ACsize = sizeof(uint64_t) * TempArraySize;
			cl::Buffer CL_AC(Options.CL_context, CL_MEM_WRITE_ONLY | CL_MEM_COPY_HOST_PTR, ACsize, TempArray, &err);

			//Setup Kernel
			cl::Kernel kernel(Options.CL_Program, "AutoCorr_sparseHL_AAV", &err);
			Options.checkErr(err, "Setup AutoCorr_CQ in AC1D::Calculate_AC_UW_Mapped() ");

			//Set Arguments
			kernel.setArg(0, CL_SparseList);
			kernel.setArg(1, CL_Params);
			kernel.setArg(2, CL_AC);
			const size_t &global_size = (size_t)Det.SparseHitList.size();

			//launch Kernel
			err = queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(global_size), cl::NullRange, NULL, &cl_event);
			cl_event.wait();

			//read results
			err = queue.enqueueReadBuffer(CL_AC, CL_TRUE, 0, ACsize, TempArray);
			Options.checkErr(err, "OpenCL kernel, launched in AC1D::Calculate_AC_UW_Mapped() ");

			//Free Device
			Options.OCL_FreeDevice(OpenCLDeviceNumber);

			//Reduce mapped data and convert to double
			int j = 0;
			for (unsigned int i = 0; i < TempArraySize; i++)
			{
				if (j == VecSize)
					j = 0;


				AC_M[j] += ((double)TempArray[i] / (double)Multiplicator);
				j++;
			}

			//free Memory
			delete[] TempArray;
		}
	}

	//Free in thread distributed Det memory:
	//delete[] Det.Intensity;

	if (Options.echo)
	{
		g_echo_mutex.lock();
		std::cout << "AC (uw)-Thread " << LowerBound << " - " << UpperBound << " Finished." << std::endl;
		g_echo_mutex.unlock();
	}
}

void AC1D::Calculate_AC_UW_MR(Settings & Options, Detector & RefDet, Settings::Interpolation IterpolMode, std::array<float,2> Photonisation, int Threads )
{
	//int Threads = 200; //(200) set higher than expectred threads because of waitingtimes for read from file
	if (Options.HitEvents.size() < 200)
	{
		Threads = (int)Options.HitEvents.size();
	}
	
	if (Options.HitEvents.size() <= 0)
	{
		std::cerr << "ERROR: No entrys in HitEvents\n";
		std::cerr << "    -> in AC1D::Calculate_AC_M()\n";
		throw;
	}

	int WorkerSize = (int)Options.HitEvents.size() / (Threads - 1);

	std::vector<std::array<unsigned int,2>> WorkerBounds;
	for (unsigned int i = 0; i < Threads; i++)
	{
		std::array<unsigned int, 2> t;
		t[0] = i * WorkerSize;
		if (t[0] + WorkerSize < Options.HitEvents.size())
		{
			t[1] = t[0] + WorkerSize;
		}
		else
		{
			t[1] = Options.HitEvents.size();
		}
	//	std::cout << t[0] << " - " << t[1] << "\n";
		WorkerBounds.push_back(t);
	}

	std::vector<double *> AC_Map;
	for (int i = 0; i < Threads; i++)
	{
		AC_Map.push_back(new double[Shape.Size]());
	}


	//Run AutoCorrelations (Map)
	std::vector<std::thread> AC_Threads;
	for (int i = 0; i < Threads; i++)
	{
		AC_Threads.push_back(std::thread(Calculate_AC_UW_Mapped,std::ref(Options), std::ref(RefDet), std::ref(AC_Map[i]), WorkerBounds[i][0], WorkerBounds[i][1], IterpolMode, Photonisation, Shape.Max_Q, Shape.dq_per_Step));
	}

	for (int i = 0; i < Threads; i++)
	{
		AC_Threads[i].join();
	}


	//create AC_UW
	delete[] AC_UW;
	AC_UW = new double[Shape.Size]();

	//Reduce
	for (int i = 0; i < Threads; i++)
	{
		ArrayOperators::ParAdd(AC_UW, AC_Map[i], Shape.Size);
	}

	//Free Memory
	for (int i = 0; i < AC_Map.size(); i++)
	{
		delete[] AC_Map[i];
	}
}
// </AC_UW sparse>

void AC1D::CreateQVector()
{
	delete[] Q;
	Q = new double[Shape.Size];
	for (int i = 0; i < Shape.Size; i++)
	{
		Q[i] = i * Shape.dq_per_Step;
	}
}

void AC1D::CalcAC()
{
	delete[] AC;
	AC = new double[Shape.Size]();

	for (int i = 0; i < Shape.Size; i++)
	{
		AC[i] = AC_UW[i] / CQ[i];
		if (std::isnan(AC[i]))
			AC[i] = 0;
	}
}