//////////////////////////////////////////////////////////////////////////////////////
//
// (C) Daniel Strano 2017. All rights reserved.
//
// This is a header-only, quick-and-dirty, multithreaded, universal quantum register
// simulation, allowing (nonphysical) register cloning and direct measurement of
// probability and phase, to leverage what advantages classical emulation of qubits
// can have.
//
// Licensed under the GNU General Public License V3.
// See LICENSE.md in the project root or https://www.gnu.org/licenses/gpl-3.0.en.html
// for details.

#include "qrack.hpp"
#include <iostream>

//#define Complex16 std::complex<double>
#define Complex16 Complex16Simd
#define bitLenInt uint8_t
#define bitCapInt uint64_t
#define bitsInByte 8

namespace Qrack {
	template <class BidirectionalIterator>
	void reverse (BidirectionalIterator first, BidirectionalIterator last, bitCapInt stride)
	{
	  while ((first < last) && (first < (last - stride))) {
		last -= stride;
		std::iter_swap (first,last);
		first += stride;
	  }
	}

	template <class BidirectionalIterator>
	void rotate (BidirectionalIterator first, BidirectionalIterator middle, BidirectionalIterator last,  bitCapInt stride)
	{
		reverse(first, middle, stride);
		reverse(middle, last, stride);
		reverse(first, last, stride);
	}

	/// The "Qrack::CoherentUnit" class represents one or more coherent quantum processor registers		
	/** The "Qrack::CoherentUnit" class represents one or more coherent quantum processor registers, including primitive bit logic gates and (abstract) opcodes-like methods. */
	//Public CoherentUnit Methods:
	///Initialize a coherent unit with qBitCount number of bits, all to |0> state.
	CoherentUnit::CoherentUnit(bitLenInt qBitCount) : rand_distribution(0.0, 1.0) {
		if (qBitCount > (sizeof(bitCapInt) * bitsInByte))
			throw std::invalid_argument("Cannot instantiate a register with greater capacity than native types on emulating system.");

		rand_generator.seed(std::time(0));

		double angle = Rand() * 2.0 * M_PI;
		runningNorm = 1.0;
		qubitCount = qBitCount;
		maxQPower = 1<<qBitCount;
		std::unique_ptr<Complex16[]> sv(new Complex16[maxQPower]);
		stateVec.reset(); 
		stateVec = std::move(sv);
		std::fill(&(stateVec[0]), &(stateVec[0]) + maxQPower, Complex16(0.0,0.0));
		stateVec[0] = Complex16(cos(angle), sin(angle));

		//InitOCL();
	}
	///Initialize a coherent unit with qBitCount number pf bits, to initState unsigned integer permutation state
	CoherentUnit::CoherentUnit(bitLenInt qBitCount, bitCapInt initState) : rand_distribution(0.0, 1.0) {
		rand_generator.seed(std::time(0));

		double angle = Rand() * 2.0 * M_PI;
		runningNorm = 1.0;
		qubitCount = qBitCount;
		maxQPower = 1<<qBitCount;
		std::unique_ptr<Complex16[]> sv(new Complex16[maxQPower]); 
		stateVec.reset(); 
		stateVec = std::move(sv);
		std::fill(&(stateVec[0]), &(stateVec[0]) + maxQPower, Complex16(0.0,0.0));
		stateVec[initState] = Complex16(cos(angle), sin(angle));

		//InitOCL();
	}
	///PSEUDO-QUANTUM Initialize a cloned register with same exact quantum state as pqs
	CoherentUnit::CoherentUnit(const CoherentUnit& pqs) : rand_distribution(0.0, 1.0) {
		rand_generator.seed(std::time(0));

		runningNorm = pqs.runningNorm;
		qubitCount = pqs.qubitCount;
		maxQPower = pqs.maxQPower;
		std::unique_ptr<Complex16[]> sv(new Complex16[maxQPower]);
		stateVec.reset(); 
		stateVec = std::move(sv);
		std::copy(&(pqs.stateVec[0]), &(pqs.stateVec[0]) + maxQPower, &(stateVec[0]));

		//InitOCL();
	}

	///Get the count of bits in this register
	int CoherentUnit::GetQubitCount() {
		return qubitCount;
	}
	///PSEUDO-QUANTUM Output the exact quantum state of this register as a permutation basis array of complex numbers
	void CoherentUnit::CloneRawState(Complex16* output) {
		if (runningNorm != 1.0) NormalizeState();
		std::copy(&(stateVec[0]), &(stateVec[0]) + maxQPower, &(output[0]));
	}
	///Generate a random double from 0 to 1
	double CoherentUnit::Rand() {
		return rand_distribution(rand_generator);
	}
	///Set |0>/|1> bit basis pure quantum permutation state, as an unsigned int
	void CoherentUnit::SetPermutation(bitCapInt perm) {
		double angle = Rand() * 2.0 * M_PI;

		runningNorm = 1.0;
		std::fill(&(stateVec[0]), &(stateVec[0]) + maxQPower, Complex16(0.0,0.0));
		stateVec[perm] = Complex16(cos(angle), sin(angle));
	}
	///Set arbitrary pure quantum state, in unsigned int permutation basis
	void CoherentUnit::SetQuantumState(Complex16* inputState) {
		std::copy(&(inputState[0]), &(inputState[0]) + maxQPower, &(stateVec[0]));
	}
	///Combine (a copy of) another CoherentUnit with this one, after the last bit index of this one.
	/** Combine (a copy of) another CoherentUnit with this one, after the last bit index of this one. (If the programmer doesn't want to "cheat," it is left up to them to delete the old coherent unit that was added. */
	void CoherentUnit::Cohere(CoherentUnit &toCopy) {
		if (runningNorm != 1.0) NormalizeState();
		if (toCopy.runningNorm != 1.0) toCopy.NormalizeState();

		bitCapInt i;
		bitCapInt nQubitCount = qubitCount + toCopy.qubitCount;
		bitCapInt nMaxQPower = 1<<nQubitCount;
		bitCapInt startMask = 0;
		bitCapInt endMask = 0;
		for (i = 0; i < qubitCount; i++) {
			startMask += (1<<i);
		}
		for (i = qubitCount; i < nQubitCount; i++) {
			endMask += (1<<i);
		}
		double angle = Rand() * 2.0 * M_PI;
		Complex16 phaseFac(cos(angle), sin(angle));
		std::unique_ptr<Complex16[]> nStateVec(new Complex16[nMaxQPower]);
		for (i = 0; i < nMaxQPower; i++) {
			nStateVec[i] = phaseFac * sqrt(norm(stateVec[(i & startMask)]) * norm(toCopy.stateVec[((i & endMask)>>qubitCount)]));
		}
		//queue.enqueueUnmapMemObject(stateBuffer, &(stateVec[0]));
		stateVec.reset();
		stateVec = std::move(nStateVec);
		qubitCount = nQubitCount;
		maxQPower = 1<<nQubitCount;
		//ReInitOCL();

		UpdateRunningNorm();
	}
	///Minimally decohere a set of contigious bits from the full coherent unit.
	/** Minimally decohere a set of contigious bits from the full coherent unit. The length of this coherent unit is reduced by the length of bits decohered, and the bits removed are output in the destination CoherentUnit pointer. The destination object must be initialized to the correct number of bits, in 0 permutation state. */
	void CoherentUnit::Decohere(bitLenInt start, bitLenInt length, CoherentUnit& destination) {
		if (runningNorm != 1.0) NormalizeState();
		
		bitLenInt end = start + length;
		bitCapInt mask = 0;
		bitCapInt startMask = 0;
		bitCapInt endMask = 0;
		bitCapInt partPower = 1<<length;
		bitCapInt remainderPower = 1<<(qubitCount - length);
		bitCapInt i;				
		for (i = start; i < end; i++) {
			mask += (1<<i);
		}
		for (i = 0; i < start; i++) {
			startMask += (1<<i);
		}
		for (i = end; i < qubitCount; i++) {
			endMask += (1<<i);
		}
		
		std::unique_ptr<double[]> partStateProb(new double[partPower]());
		std::unique_ptr<double[]> remainderStateProb(new double[remainderPower]());
		double prob;
		for (i = 0; i < maxQPower; i++) {
			prob = norm(stateVec[i]);
			partStateProb[(i & mask)>>start] += prob;
			remainderStateProb[(i & startMask) + ((i & endMask)>>length)] += prob;
		}
		//queue.enqueueUnmapMemObject(stateBuffer, &(stateVec[0]));
		stateVec.reset();
		std::unique_ptr<Complex16[]> sv(new Complex16[remainderPower]());
		stateVec = std::move(sv);
		qubitCount = qubitCount - length;
		maxQPower = 1<<qubitCount;
		//ReInitOCL();

		double angle = Rand() * 2.0 * M_PI;
		Complex16 phaseFac(cos(angle), sin(angle));
		double totProb = 0.0;
		for (i = 0; i < partPower; i++) {
			totProb += partStateProb[i];
		}
		if (totProb == 0.0) {
			destination.stateVec[0] = phaseFac;
		}
		else {
			for (i = 0; i < partPower; i++) {
				destination.stateVec[i] = sqrt(partStateProb[i] / totProb) * phaseFac;
			}
		}

		angle = Rand() * 2.0 * M_PI;
		phaseFac = Complex16(cos(angle), sin(angle));
		totProb = 0.0;
		for (i = 0; i < remainderPower; i++) {
			totProb += remainderStateProb[i];
		}
		if (totProb == 0.0) {
			stateVec[0] = phaseFac;
		}
		else {
			for (i = 0; i < remainderPower; i++) {
				stateVec[i] = sqrt(remainderStateProb[i] / totProb) * phaseFac;
			}
		}

		UpdateRunningNorm();
		destination.UpdateRunningNorm();
	}

	void CoherentUnit::Dispose(bitLenInt start, bitLenInt length) {
		if (runningNorm != 1.0) NormalizeState();
		
		bitLenInt end = start + length;
		bitCapInt startMask = 0;
		bitCapInt endMask = 0;
		bitCapInt remainderPower = 1<<(qubitCount - length);
		bitCapInt i;				
		for (i = 0; i < start; i++) {
			startMask += (1<<i);
		}
		for (i = end; i < qubitCount; i++) {
			endMask += (1<<i);
		}
		
		std::unique_ptr<double[]> remainderStateProb(new double[remainderPower]());
		for (i = 0; i < maxQPower; i++) {
			remainderStateProb[(i & startMask) + ((i & endMask)>>length)] += norm(stateVec[i]);
		}
		//queue.enqueueUnmapMemObject(stateBuffer, &(stateVec[0]));
		stateVec.reset();
		std::unique_ptr<Complex16[]> sv(new Complex16[remainderPower]());
		stateVec = std::move(sv);
		qubitCount = qubitCount - length;
		maxQPower = 1<<qubitCount;
		//ReInitOCL();

		double angle = Rand() * 2.0 * M_PI;
		Complex16 phaseFac(cos(angle), sin(angle));
		double totProb = 0.0;
		for (i = 0; i < remainderPower; i++) {
			totProb += remainderStateProb[i];
		}
		if (totProb == 0.0) {
			stateVec[0] = phaseFac;
		}
		else {
			for (i = 0; i < remainderPower; i++) {
				stateVec[i] = sqrt(remainderStateProb[i] / totProb) * phaseFac;
			}
		}

		UpdateRunningNorm();
	}

	//Logic Gates:
	///"AND" compare two bits in CoherentUnit, and store result in outputBit
	void CoherentUnit::AND(bitLenInt inputBit1, bitLenInt inputBit2, bitLenInt outputBit) {
		if (!((inputBit1 == inputBit2) && (inputBit2 == outputBit))) {
			if ((inputBit1 == outputBit) || (inputBit2 == outputBit)) {
				CoherentUnit extraBit(1, 0);
				Cohere(extraBit);
				CCNOT(inputBit1, inputBit2, qubitCount - 1);
				Swap(qubitCount - 1, outputBit);
				Dispose(qubitCount - 1, 1);
			}
			else {
				SetBit(outputBit, false);
				if (inputBit1 == inputBit2) {
					CNOT(inputBit1, outputBit);
				}
				else {
					CCNOT(inputBit1, inputBit2, outputBit);
				}
			}
		}
	}
	///"AND" compare a qubit in CoherentUnit with a classical bit, and store result in outputBit
	void CoherentUnit::CLAND(bitLenInt inputQBit, bool inputClassicalBit, bitLenInt outputBit) {
		if (!(inputClassicalBit && inputQBit == outputBit)) {
			SetBit(outputBit, false);
			if (inputClassicalBit) {
				CNOT(inputQBit, outputBit);
			}
		}
	}
	///"OR" compare two bits in CoherentUnit, and store result in outputBit
	void CoherentUnit::OR(bitLenInt inputBit1, bitLenInt inputBit2, bitLenInt outputBit) {
		if (!((inputBit1 == inputBit2) && (inputBit2 == outputBit))) {
			if ((inputBit1 == outputBit) || (inputBit2 == outputBit)) {
				CoherentUnit extraBit(1, 1);
				Cohere(extraBit);
				AntiCCNOT(inputBit1, inputBit2, qubitCount - 1);
				Swap(qubitCount - 1, outputBit);
				Dispose(qubitCount - 1, 1);
			}
			else {
				SetBit(outputBit, true);
				if (inputBit1 == inputBit2) {
					AntiCNOT(inputBit1, outputBit);
				}
				else {
					AntiCCNOT(inputBit1, inputBit2, outputBit);
				}
			}
		}
	}
	///"OR" compare a qubit in CoherentUnit with a classical bit, and store result in outputBit
	void CoherentUnit::CLOR(bitLenInt inputQBit, bool inputClassicalBit, bitLenInt outputBit) {
		if (!(!inputClassicalBit && inputQBit == outputBit)) {
			if (inputClassicalBit) {
				SetBit(outputBit, true);
			}
			else {
				SetBit(outputBit, false);
				CNOT(inputQBit, outputBit);
			}
		}
	}
	///"XOR" compare two bits in CoherentUnit, and store result in outputBit
	void CoherentUnit::XOR(bitLenInt inputBit1, bitLenInt inputBit2, bitLenInt outputBit) {
		if (((inputBit1 == inputBit2) && (inputBit2 == outputBit))) {
			SetBit(outputBit, false);
		}
		else {
			if ((inputBit1 == outputBit) || (inputBit2 == outputBit)) {
				CoherentUnit extraBit(1, 0);
				Cohere(extraBit);
				CNOT(inputBit1, qubitCount - 1);
				CNOT(inputBit2, qubitCount - 1);
				Swap(qubitCount - 1, outputBit);
				Dispose(qubitCount - 1, 1);
			}
			else {
				SetBit(outputBit, false);
				CNOT(inputBit1, outputBit);
				CNOT(inputBit2, outputBit);
			}
		}
	}
	///"XOR" compare a qubit in CoherentUnit with a classical bit, and store result in outputBit
	void CoherentUnit::CLXOR(bitLenInt inputQBit, bool inputClassicalBit, bitLenInt outputBit) {
		if (!(!inputClassicalBit && inputQBit == outputBit)) {
			if (inputClassicalBit) {
				SetBit(outputBit, true);
				CNOT(inputQBit, outputBit);
			}
			else {
				SetBit(outputBit, false);
				CNOT(inputQBit, outputBit);
			}
		}
	}
	/// Doubly-controlled not
	void CoherentUnit::CCNOT(bitLenInt control1, bitLenInt control2, bitLenInt target) {
		//if ((control1 >= qubitCount) || (control2 >= qubitCount))
		//	throw std::invalid_argument("CCNOT tried to operate on bit index greater than total bits.");
		if (control1 == control2) throw std::invalid_argument("CCNOT control bits cannot be same bit.");
		if (control1 == target || control2 == target)
			throw std::invalid_argument("CCNOT control bits cannot also be target.");

		const Complex16 pauliX[4] = {
			Complex16(0.0, 0.0), Complex16(1.0, 0.0),
			Complex16(1.0, 0.0), Complex16(0.0, 0.0)
		};

		bitCapInt qPowers[4];
		bitCapInt qPowersSorted[3];
		qPowers[1] = 1 << control1;
		qPowersSorted[0] = qPowers[1];
		qPowers[2] = 1 << control2;
		qPowersSorted[1] = qPowers[2];
		qPowers[3] = 1 << target;
		qPowersSorted[2] = qPowers[3];
		qPowers[0] = qPowers[1] + qPowers[2] + qPowers[3];
		std::sort(qPowersSorted, qPowersSorted + 3);
		Apply2x2(qPowers[0], qPowers[1] + qPowers[2], pauliX, 3, qPowersSorted, false, false);
	}
	/// "Anti-doubly-controlled not" - Apply "not" if control bits are both zero, do not apply if either control bit is one.
	void CoherentUnit::AntiCCNOT(bitLenInt control1, bitLenInt control2, bitLenInt target) {
		//if ((control1 >= qubitCount) || (control2 >= qubitCount))
		//	throw std::invalid_argument("CCNOT tried to operate on bit index greater than total bits.");
		if (control1 == control2) throw std::invalid_argument("CCNOT control bits cannot be same bit.");
		if (control1 == target || control2 == target)
			throw std::invalid_argument("CCNOT control bits cannot also be target.");

		const Complex16 pauliX[4] = {
			Complex16(0.0, 0.0), Complex16(1.0, 0.0),
			Complex16(1.0, 0.0), Complex16(0.0, 0.0)
		};

		bitCapInt qPowers[4];
		bitCapInt qPowersSorted[3];
		qPowers[1] = 1 << control1;
		qPowersSorted[0] = qPowers[1];
		qPowers[2] = 1 << control2;
		qPowersSorted[1] = qPowers[2];
		qPowers[3] = 1 << target;
		qPowersSorted[2] = qPowers[3];
		qPowers[0] = qPowers[1] + qPowers[2] + qPowers[3];
		std::sort(qPowersSorted, qPowersSorted + 3);
		Apply2x2(0, qPowers[3], pauliX, 3, qPowersSorted, false, false);
	}
	///Controlled not
	void CoherentUnit::CNOT(bitLenInt control, bitLenInt target) {
		//if ((control >= qubitCount) || (target >= qubitCount))
		//	throw std::invalid_argument("CNOT tried to operate on bit index greater than total bits.");
		if (control == target) throw std::invalid_argument("CNOT control bit cannot also be target.");
		const Complex16 pauliX[4] = {
			Complex16(0.0, 0.0), Complex16(1.0, 0.0),
			Complex16(1.0, 0.0), Complex16(0.0, 0.0)
		};
		ApplyControlled2x2(control, target, pauliX, false);
	}
	///"Anti-controlled not" - Apply "not" if control bit is zero, do not apply if control bit is one.
	void CoherentUnit::AntiCNOT(bitLenInt control, bitLenInt target) {
		//if ((control >= qubitCount) || (target >= qubitCount))
		//	throw std::invalid_argument("CNOT tried to operate on bit index greater than total bits.");
		if (control == target) throw std::invalid_argument("CNOT control bit cannot also be target.");
		const Complex16 pauliX[4] = {
			Complex16(0.0, 0.0), Complex16(1.0, 0.0),
			Complex16(1.0, 0.0), Complex16(0.0, 0.0)
		};
		ApplyAntiControlled2x2(control, target, pauliX, false);
	}
	///Hadamard gate
	void CoherentUnit::H(bitLenInt qubitIndex) {
		//if (qubitIndex >= qubitCount) throw std::invalid_argument("H tried to operate on bit index greater than total bits.");
		const Complex16 had[4] = {
			Complex16(1.0 / M_SQRT2, 0.0), Complex16(1.0 / M_SQRT2, 0.0),
			Complex16(1.0 / M_SQRT2, 0.0), Complex16(-1.0 / M_SQRT2, 0.0)
		};
		ApplySingleBit(qubitIndex, had, true);
	}
	///Measurement gate
	bool CoherentUnit::M(bitLenInt qubitIndex) {
		if (runningNorm != 1.0) NormalizeState();

		bool result;
		double prob = rand_distribution(rand_generator);
		double angle = rand_distribution(rand_generator) * 2.0 * M_PI;
		Complex16 phaseFac(cos(angle), sin(angle));

		bitCapInt qPower = 1 << qubitIndex;
		double oneChance = Prob(qubitIndex);

		result = (prob < oneChance) && oneChance > 0.0;
		double nrmlzr = 1.0;
		bitCapInt lcv;
		if (result) {
			if (oneChance > 0.0) nrmlzr = oneChance;
			phaseFac = phaseFac / nrmlzr;
			for (lcv = 0; lcv < maxQPower; lcv++) {
				if ((lcv & qPower) == 0) {
					stateVec[lcv] = Complex16(0.0, 0.0);
				}
				else {
					stateVec[lcv] = phaseFac * stateVec[lcv];;
				}
			}
		}
		else {
			if (oneChance < 1.0) nrmlzr = sqrt(1.0 - oneChance);
			phaseFac = phaseFac / nrmlzr;
			for (lcv = 0; lcv < maxQPower; lcv++) {
				if ((lcv & qPower) == 0) {
					stateVec[lcv] = phaseFac * stateVec[lcv];
				}
				else {
					stateVec[lcv] = Complex16(0.0, 0.0);
				}
			}
		}

		UpdateRunningNorm();

		return result;
	}
	///PSEUDO-QUANTUM Direct measure of bit probability to be in |1> state
	double CoherentUnit::Prob(bitLenInt qubitIndex) {
		if (runningNorm != 1.0) NormalizeState();

		bitCapInt qPower = 1 << qubitIndex;
		double oneChance = 0;
		bitCapInt lcv;
		for (lcv = 0; lcv < maxQPower; lcv++) {
			if ((lcv & qPower) == qPower) {
				oneChance += norm(stateVec[lcv]);
			} 
		}

		return oneChance;
	}
	///PSEUDO-QUANTUM Direct measure of full register probability to be in permutation state
	double CoherentUnit::ProbAll(bitCapInt fullRegister) {
		if (runningNorm != 1.0) NormalizeState();

		return norm(stateVec[fullRegister]);
	}
	///PSEUDO-QUANTUM Direct measure of all bit probabilities in register to be in |1> state
	void CoherentUnit::ProbArray(double* probArray) {
		if (runningNorm != 1.0) NormalizeState();

		bitCapInt lcv;
		for (lcv = 0; lcv < maxQPower; lcv++) {
			probArray[lcv] = norm(stateVec[lcv]); 
		}
	}
	///"Phase shift gate" - Rotates as e^(-i*\theta/2) around |1> state 
	void CoherentUnit::R1(double radians, bitLenInt qubitIndex) {
		//if (qubitIndex >= qubitCount) throw std::invalid_argument("Z tried to operate on bit index greater than total bits.");
		double cosine = cos(radians / 2.0);
		double sine = sin(radians / 2.0); 
		const Complex16 mtrx[4] = {
			Complex16(1.0, 0), Complex16(0.0, 0.0),
			Complex16(0.0, 0.0), Complex16(cosine, sine)
		};
		ApplySingleBit(qubitIndex, mtrx, true);
	}
	///Dyadic fraction "phase shift gate" - Rotates as e^(i*(M_PI * numerator) / denominator) around |1> state
	/** Dyadic fraction "phase shift gate" - Rotates as e^(i*(M_PI * numerator) / denominator) around |1> state. NOTE THAT DYADIC OPERATION ANGLE SIGN IS REVERSED FROM RADIAN ROTATION OPERATORS AND LACKS DIVISION BY A FACTOR OF TWO. */
	void CoherentUnit::R1Dyad(int numerator, int denominator, bitLenInt qubitIndex) {
		//if (qubitIndex >= qubitCount) throw std::invalid_argument("Z tried to operate on bit index greater than total bits.");
		R1((M_PI * numerator * 2) / denominator, qubitIndex);
	}
	///x axis rotation gate - Rotates as e^(-i*\theta/2) around Pauli x axis 
	void CoherentUnit::RX(double radians, bitLenInt qubitIndex) {
		//if (qubitIndex >= qubitCount) throw std::invalid_argument("X tried to operate on bit index greater than total bits.");
		double cosine = cos(radians / 2.0);
		double sine = sin(radians / 2.0); 
		Complex16 pauliRX[4] = {
			Complex16(cosine, 0.0), Complex16(0.0, -sine),
			Complex16(0.0, -sine), Complex16(cosine, 0.0)
		};
		ApplySingleBit(qubitIndex, pauliRX, true);
	}
	///Dyadic fraction x axis rotation gate - Rotates as e^(i*(M_PI * numerator) / denominator) around Pauli x axis
	/** Dyadic fraction x axis rotation gate - Rotates as e^(i*(M_PI * numerator) / denominator) around Pauli x axis. NOTE THAT DYADIC OPERATION ANGLE SIGN IS REVERSED FROM RADIAN ROTATION OPERATORS AND LACKS DIVISION BY A FACTOR OF TWO. */
	void CoherentUnit::RXDyad(int numerator, int denominator, bitLenInt qubitIndex) {
		//if (qubitIndex >= qubitCount) throw std::invalid_argument("Z tried to operate on bit index greater than total bits.");
		RX((-M_PI * numerator * 2) / denominator, qubitIndex);
	}
	///y axis rotation gate - Rotates as e^(-i*\theta/2) around Pauli y axis 
	void CoherentUnit::RY(double radians, bitLenInt qubitIndex) {
		//if (qubitIndex >= qubitCount) throw std::invalid_argument("Y tried to operate on bit index greater than total bits.");
		double cosine = cos(radians / 2.0);
		double sine = sin(radians / 2.0); 
		Complex16 pauliRY[4] = {
			Complex16(cosine, 0.0), Complex16(-sine, 0.0),
			Complex16(sine, 0.0), Complex16(cosine, 0.0)
		};
		ApplySingleBit(qubitIndex, pauliRY, true);
	}
	///Dyadic fraction y axis rotation gate - Rotates as e^(i*(M_PI * numerator) / denominator) around Pauli y axis
	/** Dyadic fraction y axis rotation gate - Rotates as e^(i*(M_PI * numerator) / denominator) around Pauli y axis. NOTE THAT DYADIC OPERATION ANGLE SIGN IS REVERSED FROM RADIAN ROTATION OPERATORS AND LACKS DIVISION BY A FACTOR OF TWO. */
	void CoherentUnit::RYDyad(int numerator, int denominator, bitLenInt qubitIndex) {
		//if (qubitIndex >= qubitCount) throw std::invalid_argument("Z tried to operate on bit index greater than total bits.");
		RY((-M_PI * numerator * 2) / denominator, qubitIndex);
	}
	///z axis rotation gate - Rotates as e^(-i*\theta/2) around Pauli z axis 
	void CoherentUnit::RZ(double radians, bitLenInt qubitIndex) {
		//if (qubitIndex >= qubitCount) throw std::invalid_argument("Z tried to operate on bit index greater than total bits.");
		double cosine = cos(radians / 2.0);
		double sine = sin(radians / 2.0); 
		const Complex16 pauliRZ[4] = {
			Complex16(cosine, -sine), Complex16(0.0, 0.0),
			Complex16(0.0, 0.0), Complex16(cosine, sine)
		};
		ApplySingleBit(qubitIndex, pauliRZ, true);
	}
	///Dyadic fraction y axis rotation gate - Rotates as e^(i*(M_PI * numerator) / denominator) around Pauli y axis
	/** Dyadic fraction y axis rotation gate - Rotates as e^(i*(M_PI * numerator) / denominator) around Pauli y axis. NOTE THAT DYADIC OPERATION ANGLE SIGN IS REVERSED FROM RADIAN ROTATION OPERATORS AND LACKS DIVISION BY A FACTOR OF TWO. */
	void CoherentUnit::RZDyad(int numerator, int denominator, bitLenInt qubitIndex) {
		//if (qubitIndex >= qubitCount) throw std::invalid_argument("Z tried to operate on bit index greater than total bits.");
		RZ((-M_PI * numerator * 2) / denominator, qubitIndex);
	}
	///Set individual bit to pure |0> (false) or |1> (true) state
	void CoherentUnit::SetBit(bitLenInt qubitIndex1, bool value) {
		if (value != M(qubitIndex1)) {
			X(qubitIndex1);
		}
	}
	///Swap values of two bits in register
	void CoherentUnit::Swap(bitLenInt qubitIndex1, bitLenInt qubitIndex2) {
		//if ((qubitIndex1 >= qubitCount) || (qubitIndex2 >= qubitCount))
		//	throw std::invalid_argument("CNOT tried to operate on bit index greater than total bits.");
		if (qubitIndex1 != qubitIndex2) {
			const Complex16 pauliX[4] = {
				Complex16(0.0, 0.0), Complex16(1.0, 0.0),
				Complex16(1.0, 0.0), Complex16(0.0, 0.0)
			};

			bitCapInt qPowers[3];
			bitCapInt qPowersSorted[2];
			qPowers[1] = 1 << qubitIndex1;
			qPowers[2] = 1 << qubitIndex2;
			qPowers[0] = qPowers[1] + qPowers[2];
			if (qubitIndex1 < qubitIndex2) {
				qPowersSorted[0] = qPowers[1];
				qPowersSorted[1] = qPowers[2];
			}
			else {
				qPowersSorted[0] = qPowers[2];
				qPowersSorted[1] = qPowers[1];
			}
		
			Apply2x2(qPowers[2], qPowers[1], pauliX, 2, qPowersSorted, false, false);
		}
	}
	///NOT gate, which is also Pauli x matrix
	void CoherentUnit::X(bitLenInt qubitIndex) {
		//if (qubitIndex >= qubitCount) throw std::invalid_argument("X tried to operate on bit index greater than total bits.");
		const Complex16 pauliX[4] = {
			Complex16(0.0, 0.0), Complex16(1.0, 0.0),
			Complex16(1.0, 0.0), Complex16(0.0, 0.0)
		};
		ApplySingleBit(qubitIndex, pauliX, false);
	}
	///Apply Pauli Y matrix to bit
	void CoherentUnit::Y(bitLenInt qubitIndex) {
		//if (qubitIndex >= qubitCount) throw std::invalid_argument("Y tried to operate on bit index greater than total bits.");
		const Complex16 pauliY[4] = {
			Complex16(0.0, 0.0), Complex16(0.0, -1.0),
			Complex16(0.0, 1.0), Complex16(0.0, 0.0)
		};
		ApplySingleBit(qubitIndex, pauliY, false);
	}
	///Apply Pauli Z matrix to bit
	void CoherentUnit::Z(bitLenInt qubitIndex) {
		//if (qubitIndex >= qubitCount) throw std::invalid_argument("Z tried to operate on bit index greater than total bits.");
		const Complex16 pauliZ[4] = {
			Complex16(1.0, 0.0), Complex16(0.0, 0.0),
			Complex16(0.0, 0.0), Complex16(-1.0, 0.0)
		};
		ApplySingleBit(qubitIndex, pauliZ, false);
	}
	///Controlled "phase shift gate"
	/** Controlled "phase shift gate" - if control bit is true, rotates target bit as e^(-i*\theta/2) around |1> state */
	void CoherentUnit::CRT(double radians, bitLenInt control, bitLenInt target) {
		//if ((control >= qubitCount) || (target >= qubitCount))
		//	throw std::invalid_argument("CNOT tried to operate on bit index greater than total bits.");
		if (control == target) throw std::invalid_argument("CRT control bit cannot also be target.");
		double cosine = cos(radians / 2.0);
		double sine = sin(radians / 2.0); 
		const Complex16 mtrx[4] = {
			Complex16(1.0, 0), Complex16(0.0, 0.0),
			Complex16(0.0, 0.0), Complex16(cosine, sine)
		};
		ApplyControlled2x2(control, target, mtrx, true);
	}
	///Controlled dyadic fraction "phase shift gate"
	/** Controlled "phase shift gate" - if control bit is true, rotates target bit as e^(-i*\theta/2) around |1> state */
	void CoherentUnit::CRTDyad(int numerator, int denominator, bitLenInt control, bitLenInt target) {
		//if (qubitIndex >= qubitCount) throw std::invalid_argument("Z tried to operate on bit index greater than total bits.");
		if (control == target) throw std::invalid_argument("CRTDyad control bit cannot also be target.");
		CRT((-M_PI * numerator * 2) / denominator, control, target);
	}
	///Controlled x axis rotation
	/** Controlled x axis rotation - if control bit is true, rotates as e^(-i*\theta/2) around Pauli x axis */
	void CoherentUnit::CRX(double radians, bitLenInt control, bitLenInt target) {
		//if (qubitIndex >= qubitCount) throw std::invalid_argument("X tried to operate on bit index greater than total bits.");
		if (control == target) throw std::invalid_argument("CRX control bit cannot also be target.");
		double cosine = cos(radians / 2.0);
		double sine = sin(radians / 2.0); 
		Complex16 pauliRX[4] = {
			Complex16(cosine, 0.0), Complex16(0.0, -sine),
			Complex16(0.0, -sine), Complex16(cosine, 0.0)
		};
		ApplyControlled2x2(control, target, pauliRX, true);
	}
	///Controlled dyadic fraction x axis rotation gate - Rotates as e^(i*(M_PI * numerator) / denominator) around Pauli x axis
	/** Controlled dyadic fraction x axis rotation gate - Rotates as e^(i*(M_PI * numerator) / denominator) around Pauli x axis. NOTE THAT DYADIC OPERATION ANGLE SIGN IS REVERSED FROM RADIAN ROTATION OPERATORS. */
	void CoherentUnit::CRXDyad(int numerator, int denominator, bitLenInt control, bitLenInt target) {
		//if (qubitIndex >= qubitCount) throw std::invalid_argument("Z tried to operate on bit index greater than total bits.");
		if (control == target) throw std::invalid_argument("CRXDyad control bit cannot also be target.");
		CRX((-M_PI * numerator * 2) / denominator, control, target);
	}
	///Controlled y axis rotation
	/** Controlled y axis rotation - if control bit is true, rotates as e^(-i*\theta) around Pauli y axis */
	void CoherentUnit::CRY(double radians, bitLenInt control, bitLenInt target) {
		//if (qubitIndex >= qubitCount) throw std::invalid_argument("Y tried to operate on bit index greater than total bits.");
		if (control == target) throw std::invalid_argument("CRY control bit cannot also be target.");
		double cosine = cos(radians / 2.0);
		double sine = sin(radians / 2.0); 
		Complex16 pauliRY[4] = {
			Complex16(cosine, 0.0), Complex16(-sine, 0.0),
			Complex16(sine, 0.0), Complex16(cosine, 0.0)
		};
		ApplyControlled2x2(control, target, pauliRY, true);
	}
	///Controlled dyadic fraction y axis rotation gate - Rotates as e^(i*(M_PI * numerator) / denominator) around Pauli y axis
	/** Controlled dyadic fraction y axis rotation gate - Rotates as e^(i*(M_PI * numerator) / denominator) around Pauli y axis. NOTE THAT DYADIC OPERATION ANGLE SIGN IS REVERSED FROM RADIAN ROTATION OPERATORS. */
	void CoherentUnit::CRYDyad(int numerator, int denominator, bitLenInt control, bitLenInt target) {
		//if (qubitIndex >= qubitCount) throw std::invalid_argument("Z tried to operate on bit index greater than total bits.");
		if (control == target) throw std::invalid_argument("CRYDyad control bit cannot also be target.");
		CRY((-M_PI * numerator * 2) / denominator, control, target);
	}
	///Controlled z axis rotation
	/** Controlled z axis rotation - if control bit is true, rotates as e^(-i*\theta) around Pauli z axis */
	void CoherentUnit::CRZ(double radians, bitLenInt control, bitLenInt target) {
		//if (qubitIndex >= qubitCount) throw std::invalid_argument("Z tried to operate on bit index greater than total bits.");
		if (control == target) throw std::invalid_argument("CRZ control bit cannot also be target.");
		double cosine = cos(radians / 2.0);
		double sine = sin(radians / 2.0); 
		const Complex16 pauliRZ[4] = {
			Complex16(cosine, -sine), Complex16(0.0, 0.0),
			Complex16(0.0, 0.0), Complex16(cosine, sine)
		};
		ApplyControlled2x2(control, target, pauliRZ, true);
	}
	///Controlled dyadic fraction z axis rotation gate - Rotates as e^(i*(M_PI * numerator) / denominator) around Pauli z axis
	/** Controlled dyadic fraction z axis rotation gate - Rotates as e^(i*(M_PI * numerator) / denominator) around Pauli z axis. NOTE THAT DYADIC OPERATION ANGLE SIGN IS REVERSED FROM RADIAN ROTATION OPERATORS. */
	void CoherentUnit::CRZDyad(int numerator, int denominator, bitLenInt control, bitLenInt target) {
		//if (qubitIndex >= qubitCount) throw std::invalid_argument("Z tried to operate on bit index greater than total bits.");
		if (control == target) throw std::invalid_argument("CRZDyad control bit cannot also be target.");
		CRZ((-M_PI * numerator * 2) / denominator, control, target);
	}
	///Apply controlled Pauli Y matrix to bit
	void CoherentUnit::CY(bitLenInt control, bitLenInt target) {
		//if (qubitIndex >= qubitCount) throw std::invalid_argument("Y tried to operate on bit index greater than total bits.");
		if (control == target) throw std::invalid_argument("CY control bit cannot also be target.");
		const Complex16 pauliY[4] = {
			Complex16(0.0, 0.0), Complex16(0.0, -1.0),
			Complex16(0.0, 1.0), Complex16(0.0, 0.0)
		};
		ApplyControlled2x2(control, target, pauliY, false);
	}
	///Apply controlled Pauli Z matrix to bit
	void CoherentUnit::CZ(bitLenInt control, bitLenInt target) {
		//if (qubitIndex >= qubitCount) throw std::invalid_argument("Z tried to operate on bit index greater than total bits.");
		if (control == target) throw std::invalid_argument("CZ control bit cannot also be target.");
		const Complex16 pauliZ[4] = {
			Complex16(1.0, 0.0), Complex16(0.0, 0.0),
			Complex16(0.0, 0.0), Complex16(-1.0, 0.0)
		};
		ApplyControlled2x2(control, target, pauliZ, false);
	}

	//Single register instructions:
	///Apply X ("not") gate to each bit in "length," starting from bit index "start"
	void CoherentUnit::X(bitLenInt start, bitLenInt length) {
		bitCapInt inOutMask = 0;
		bitCapInt otherMask = (1<<qubitCount) - 1;
		bitCapInt inOutRes, otherRes, i;
		for (i = 0; i < length; i++) {
			inOutMask += 1<<(start + i);
		}
		otherMask -= inOutMask;
		std::unique_ptr<Complex16[]> nStateVec(new Complex16[maxQPower]);
		for (i = 0; i < maxQPower; i++) {
			otherRes = (i & otherMask);
			inOutRes = ((~i) & inOutMask);
			nStateVec[inOutRes | otherRes] = stateVec[i];
		}
		stateVec.reset(); 
		stateVec = std::move(nStateVec);
	}
	///Apply Hadamard gate to each bit in "length," starting from bit index "start"
	void CoherentUnit::H(bitLenInt start, bitLenInt length) {
		for (bitLenInt lcv = 0; lcv < length; lcv++) {
			H(start + lcv);
		}
	}
	///"Phase shift gate" - Rotates each bit as e^(-i*\theta/2) around |1> state 
	void CoherentUnit::R1(double radians, bitLenInt start, bitLenInt length) {
		for (bitLenInt lcv = 0; lcv < length; lcv++) {
			R1(radians, start + lcv);
		}
	}
	///Dyadic fraction "phase shift gate" - Rotates each bit as e^(i*(M_PI * numerator) / denominator) around |1> state
	/** Dyadic fraction "phase shift gate" - Rotates each bit as e^(i*(M_PI * numerator) / denominator) around |1> state. NOTE THAT DYADIC OPERATION ANGLE SIGN IS REVERSED FROM RADIAN ROTATION OPERATORS AND LACKS DIVISION BY A FACTOR OF TWO. */
	void CoherentUnit::R1Dyad(int numerator, int denominator, bitLenInt start, bitLenInt length) {
		for (bitLenInt lcv = 0; lcv < length; lcv++) {
			R1Dyad(numerator, denominator, start + lcv);
		}
	}
	///x axis rotation gate - Rotates each bit as e^(-i*\theta/2) around Pauli x axis 
	void CoherentUnit::RX(double radians, bitLenInt start, bitLenInt length) {
		for (bitLenInt lcv = 0; lcv < length; lcv++) {
			RX(radians, start + lcv);
		}
	}
	///Dyadic fraction x axis rotation gate - Rotates each bit as e^(i*(M_PI * numerator) / denominator) around Pauli x axis
	/** Dyadic fraction x axis rotation gate - Rotates each bit as e^(i*(M_PI * numerator) / denominator) around Pauli x axis. NOTE THAT DYADIC OPERATION ANGLE SIGN IS REVERSED FROM RADIAN ROTATION OPERATORS AND LACKS DIVISION BY A FACTOR OF TWO. */
	void CoherentUnit::RXDyad(int numerator, int denominator, bitLenInt start, bitLenInt length) {
		for (bitLenInt lcv = 0; lcv < length; lcv++) {
			RXDyad(numerator, denominator, start + lcv);
		}
	}
	///y axis rotation gate - Rotates each bit as e^(-i*\theta/2) around Pauli y axis 
	void CoherentUnit::RY(double radians, bitLenInt start, bitLenInt length) {
		for (bitLenInt lcv = 0; lcv < length; lcv++) {
			RY(radians, start + lcv);
		}
	}
	///Dyadic fraction y axis rotation gate - Rotates each bit as e^(i*(M_PI * numerator) / denominator) around Pauli y axis
	/** Dyadic fraction y axis rotation gate - Rotates each bit as e^(i*(M_PI * numerator) / denominator) around Pauli y axis. NOTE THAT DYADIC OPERATION ANGLE SIGN IS REVERSED FROM RADIAN ROTATION OPERATORS AND LACKS DIVISION BY A FACTOR OF TWO. */
	void CoherentUnit::RYDyad(int numerator, int denominator, bitLenInt start, bitLenInt length) {
		for (bitLenInt lcv = 0; lcv < length; lcv++) {
			RYDyad(numerator, denominator, start + lcv);
		}
	}
	///z axis rotation gate - Rotates each bit as e^(-i*\theta/2) around Pauli z axis 
	void CoherentUnit::RZ(double radians, bitLenInt start, bitLenInt length) {
		for (bitLenInt lcv = 0; lcv < length; lcv++) {
			RZ(radians, start + lcv);
		}
	}
	///Dyadic fraction z axis rotation gate - Rotates each bit as e^(i*(M_PI * numerator) / denominator) around Pauli y axis
	/** Dyadic fraction z axis rotation gate - Rotates each bit as e^(i*(M_PI * numerator) / denominator) around Pauli y axis. NOTE THAT DYADIC OPERATION ANGLE SIGN IS REVERSED FROM RADIAN ROTATION OPERATORS AND LACKS DIVISION BY A FACTOR OF TWO. */
	void CoherentUnit::RZDyad(int numerator, int denominator, bitLenInt start, bitLenInt length) {
		for (bitLenInt lcv = 0; lcv < length; lcv++) {
			RZDyad(numerator, denominator, start + lcv);
		}
	}
	///Apply Pauli Y matrix to each bit
	void CoherentUnit::Y(bitLenInt start, bitLenInt length) {
		for (bitLenInt lcv = 0; lcv < length; lcv++) {
			Y(start + lcv);
		}
	}
	///Apply Pauli Z matrix to each bit
	void CoherentUnit::Z(bitLenInt start, bitLenInt length) {
		for (bitLenInt lcv = 0; lcv < length; lcv++) {
			Z(start + lcv);
		}
	}
	///Controlled "phase shift gate"
	/** Controlled "phase shift gate" - for each bit, if control bit is true, rotates target bit as e^(-i*\theta/2) around |1> state */
	void CoherentUnit::CRT(double radians, bitLenInt control, bitLenInt target, bitLenInt length) {
		for (bitLenInt lcv = 0; lcv < length; lcv++) {
			CRT(radians, control + lcv, target + lcv);
		}
	}
	///Controlled dyadic fraction "phase shift gate"
	/** Controlled "phase shift gate" - for each bit, if control bit is true, rotates target bit as e^(-i*\theta/2) around |1> state */
	void CoherentUnit::CRTDyad(int numerator, int denominator, bitLenInt control, bitLenInt target, bitLenInt length) {
		for (bitLenInt lcv = 0; lcv < length; lcv++) {
			CRTDyad(numerator, denominator, control + lcv, target + lcv);
		}
	}
	///Controlled x axis rotation
	/** Controlled x axis rotation - for each bit, if control bit is true, rotates as e^(-i*\theta/2) around Pauli x axis */
	void CoherentUnit::CRX(double radians, bitLenInt control, bitLenInt target, bitLenInt length) {
		for (bitLenInt lcv = 0; lcv < length; lcv++) {
			CRX(radians, control + lcv, target + lcv);
		}
	}
	///Controlled dyadic fraction x axis rotation gate - for each bit, if control bit is true, rotates target bit as as e^(i*(M_PI * numerator) / denominator) around Pauli x axis
	/** Controlled dyadic fraction x axis rotation gate - for each bit, if control bit is true, rotates target bit as e^(i*(M_PI * numerator) / denominator) around Pauli x axis. NOTE THAT DYADIC OPERATION ANGLE SIGN IS REVERSED FROM RADIAN ROTATION OPERATORS. */
	void CoherentUnit::CRXDyad(int numerator, int denominator, bitLenInt control, bitLenInt target, bitLenInt length) {
		for (bitLenInt lcv = 0; lcv < length; lcv++) {
			CRXDyad(numerator, denominator, control + lcv, target + lcv);
		}
	}
	///Controlled y axis rotation
	/** Controlled y axis rotation - for each bit, if control bit is true, rotates as e^(-i*\theta) around Pauli y axis */
	void CoherentUnit::CRY(double radians, bitLenInt control, bitLenInt target, bitLenInt length) {
		for (bitLenInt lcv = 0; lcv < length; lcv++) {
			CRY(radians, control + lcv, target + lcv);
		}
	}
	///Controlled dyadic fraction y axis rotation gate - for each bit, if control bit is true, rotates target bit as e^(i*(M_PI * numerator) / denominator) around Pauli y axis
	/** Controlled dyadic fraction y axis rotation gate - for each bit, if control bit is true, rotates target bit as e^(i*(M_PI * numerator) / denominator) around Pauli y axis. NOTE THAT DYADIC OPERATION ANGLE SIGN IS REVERSED FROM RADIAN ROTATION OPERATORS. */
	void CoherentUnit::CRYDyad(int numerator, int denominator, bitLenInt control, bitLenInt target, bitLenInt length) {
		for (bitLenInt lcv = 0; lcv < length; lcv++) {
			CRYDyad(numerator, denominator, control + lcv, target + lcv);
		}
	}
	///Controlled z axis rotation
	/** Controlled z axis rotation - for each bit, if control bit is true, rotates as e^(-i*\theta) around Pauli z axis */
	void CoherentUnit::CRZ(double radians, bitLenInt control, bitLenInt target, bitLenInt length) {
		for (bitLenInt lcv = 0; lcv < length; lcv++) {
			CRZ(radians, control + lcv, target + lcv);
		}
	}
	///Controlled dyadic fraction z axis rotation gate - for each bit, if control bit is true, rotates target bit as e^(i*(M_PI * numerator) / denominator) around Pauli z axis
	/** Controlled dyadic fraction z axis rotation gate - for each bit, if control bit is true, rotates target bit as e^(i*(M_PI * numerator) / denominator) around Pauli z axis. NOTE THAT DYADIC OPERATION ANGLE SIGN IS REVERSED FROM RADIAN ROTATION OPERATORS. */
	void CoherentUnit::CRZDyad(int numerator, int denominator, bitLenInt control, bitLenInt target, bitLenInt length) {
		for (bitLenInt lcv = 0; lcv < length; lcv++) {
			CRZDyad(numerator, denominator, control + lcv, target + lcv);
		}
	}
	///Apply controlled Pauli Y matrix to each bit
	void CoherentUnit::CY(bitLenInt control, bitLenInt target, bitLenInt length) {
		for (bitLenInt lcv = 0; lcv < length; lcv++) {
			CY(control + lcv, target + lcv);
		}
	}
	///Apply controlled Pauli Z matrix to each bit
	void CoherentUnit::CZ(bitLenInt control, bitLenInt target, bitLenInt length) {
		for (bitLenInt lcv = 0; lcv < length; lcv++) {
			CZ(control + lcv, target + lcv);
		}
	}
	///"AND" compare two bit ranges in CoherentUnit, and store result in range starting at output
	void CoherentUnit::AND(bitLenInt inputStart1, bitLenInt inputStart2, bitLenInt outputStart, bitLenInt length) {
		if (!((inputStart1 == inputStart2) && (inputStart2 == outputStart))) {
			for (bitLenInt i = 0; i < length; i++) {
				AND(inputStart1 + i, inputStart2 + i, outputStart + i);
			}
		}
	}
	///"AND" compare a bit range in CoherentUnit with a classical unsigned integer, and store result in range starting at output
	void CoherentUnit::CLAND(bitLenInt qInputStart, bitCapInt classicalInput, bitLenInt outputStart, bitLenInt length) {
		bool cBit;
		for (bitLenInt i = 0; i < length; i++) {
			cBit = (1<<i) & classicalInput;
			CLAND(qInputStart + i, cBit, outputStart + i);
		}
	}
	///"OR" compare two bit ranges in CoherentUnit, and store result in range starting at output
	void CoherentUnit::OR(bitLenInt inputStart1, bitLenInt inputStart2, bitLenInt outputStart, bitLenInt length) {
		if (!((inputStart1 == inputStart2) && (inputStart2 == outputStart))) {
			for (bitLenInt i = 0; i < length; i++) {
				OR(inputStart1 + i, inputStart2 + i, outputStart + i);
			}
		}
	}
	///"OR" compare a bit range in CoherentUnit with a classical unsigned integer, and store result in range starting at output
	void CoherentUnit::CLOR(bitLenInt qInputStart, bitCapInt classicalInput, bitLenInt outputStart, bitLenInt length) {
		bool cBit;
		for (bitLenInt i = 0; i < length; i++) {
			cBit = (1<<i) & classicalInput;
			CLOR(qInputStart + i, cBit, outputStart + i);
		}
	}
	///"XOR" compare two bit ranges in CoherentUnit, and store result in range starting at output
	void CoherentUnit::XOR(bitLenInt inputStart1, bitLenInt inputStart2, bitLenInt outputStart, bitLenInt length) {
		if (!((inputStart1 == inputStart2) && (inputStart2 == outputStart))) {
			for (bitLenInt i = 0; i < length; i++) {
				XOR(inputStart1 + i, inputStart2 + i, outputStart + i);
			}
		}
	}
	///"XOR" compare a bit range in CoherentUnit with a classical unsigned integer, and store result in range starting at output
	void CoherentUnit::CLXOR(bitLenInt qInputStart, bitCapInt classicalInput, bitLenInt outputStart, bitLenInt length) {
		bool cBit;
		for (bitLenInt i = 0; i < length; i++) {
			cBit = (1<<i) & classicalInput;
			CLXOR(qInputStart + i, cBit, outputStart + i);
		}
	}
	///Arithmetic shift left, with last 2 bits as sign and carry
	void CoherentUnit::ASL(bitLenInt shift, bitLenInt start, bitLenInt length) {
		if ((length > 0) && (shift > 0)) {
			int i;
			bitLenInt end = start + length;
			if (shift >= length) {
				for (i = start; i < end; i++) {
					SetBit(i, false);
				}
			}
			else {						
				Swap(end - 1, end - 2);
				Reverse(start, end);
				Reverse(start, start + shift);
				Reverse(start + shift, end);
				Swap(end - 1, end - 2);

				for (i = 0; i < shift; i++) {
					SetBit(i, false);
				}
			}
		}
	}
	///Arithmetic shift right, with last 2 bits as sign and carry
	void CoherentUnit::ASR(bitLenInt shift, bitLenInt start, bitLenInt length) {
		if ((length > 0) && (shift > 0)) {
			int i;
			bitLenInt end = start + length;
			if (shift >= length) {
				for (i = start; i < end; i++) {
					SetBit(i, false);
				}
			}
			else {	
				Swap(end - 1, end - 2);
				Reverse(start + shift, end);
				Reverse(start, start + shift);
				Reverse(start, end);
				Swap(end - 1, end - 2);

				for (i = start; i < shift; i++) {
					SetBit(end - i - 1, false);
				}
			}
		}
	}
	///Logical shift left, filling the extra bits with |0>
	void CoherentUnit::LSL(bitLenInt shift, bitLenInt start, bitLenInt length) {
		if ((length > 0) && (shift > 0)) {
			int i;
			bitLenInt end = start + length;
			if (shift >= length) {
				for (i = start; i < end; i++) {
					SetBit(i, false);
				}
			}
			else {	
				ROL(shift, start, length);
				for (i = start; i < shift; i++) {
					SetBit(i, false);
				}
			}
		}
	}
	///Logical shift right, filling the extra bits with |0>
	void CoherentUnit::LSR(bitLenInt shift, bitLenInt start, bitLenInt length) {
		if ((length > 0) && (shift > 0)) {
			int i;
			bitLenInt end = start + length;
			if (shift >= length) {
				for (i = start; i < end; i++) {
					SetBit(i, false);
				}
			}
			else {	
				ROR(shift, start, length);
				for (i = start; i < shift; i++) {
					SetBit(end - i - 1, false);
				}
			}
		}
	}
	/// "Circular shift left" - shift bits left, and carry last bits.
	void CoherentUnit::ROL(bitLenInt shift, bitLenInt start, bitLenInt length) {
		bitCapInt regMask = 0;
		bitCapInt otherMask = (1<<qubitCount) - 1;
		bitCapInt lengthMask = (1<<length) - 1;
		bitCapInt otherRes, regRes, regInt, outInt, i;
		for (i = 0; i < length; i++) {
			regMask += 1<<(start + i);
		}
		otherMask -= regMask;
		std::unique_ptr<Complex16[]> nStateVec(new Complex16[maxQPower]);
		for (i = 0; i < maxQPower; i++) {
			otherRes = (i & otherMask);
			regRes = (i & regMask);
			regInt = regRes>>start;
			outInt = (regInt>>(length - shift)) | ((regInt<<shift) & lengthMask);
			nStateVec[(outInt<<start) | otherRes] = stateVec[i];
		}
		stateVec.reset(); 
		stateVec = std::move(nStateVec);
	}
	/// "Circular shift right" - shift bits right, and carry first bits.
	void CoherentUnit::ROR(bitLenInt shift, bitLenInt start, bitLenInt length) {
		bitCapInt regMask = 0;
		bitCapInt otherMask = (1<<qubitCount) - 1;
		bitCapInt lengthMask = (1<<length) - 1;
		bitCapInt otherRes, regRes, regInt, outInt, i;
		for (i = 0; i < length; i++) {
			regMask += 1<<(start + i);
		}
		otherMask -= regMask;
		std::unique_ptr<Complex16[]> nStateVec(new Complex16[maxQPower]);
		for (i = 0; i < maxQPower; i++) {
			otherRes = (i & otherMask);
			regRes = (i & regMask);
			regInt = regRes>>start;
			outInt = (regInt>>shift) | ((regInt<<(length - shift)) & lengthMask);
			nStateVec[(outInt<<start) | otherRes] = stateVec[i];
		}
		stateVec.reset(); 
		stateVec = std::move(nStateVec);
	}
	///Add integer (without sign)
	void CoherentUnit::INC(bitCapInt toAdd, bitLenInt start, bitLenInt length) {
		bitCapInt lengthPower = 1<<length;
		toAdd %= lengthPower;
		if ((length > 0) && (toAdd > 0)) {
			bitCapInt i, j;
			bitLenInt end = start + length;
			bitCapInt startPower = 1<<start;
			bitCapInt endPower = 1<<end;
			bitCapInt iterPower = 1<<(qubitCount - end);
			bitCapInt maxLCV = iterPower * endPower;
			for (i = 0; i < startPower; i++) {
				for (j = 0; j < maxLCV; j+=endPower) {
					rotate(&(stateVec[0]) + i + j,
						  &(stateVec[0]) + ((lengthPower - toAdd) * startPower) + i + j,
						  &(stateVec[0]) + endPower + i + j,
						  startPower);
				}
			}
		}
	}
	///Add BCD integer (without sign)
	void CoherentUnit::INCBCD(bitCapInt toAdd, bitLenInt inOutStart, bitLenInt length) {
		bitCapInt nibbleCount = length / 4;
		if (nibbleCount * 4 != length) {
			throw std::invalid_argument("BCD word bit length must be a multiple of 4.");
		}
		bitCapInt inOutMask = 0;
		bitCapInt otherMask = (1<<qubitCount) - 1;
		bitCapInt inOutRes, otherRes, inOutInt, outInt, partToAdd, i, j;
		bool isValid;
		char test1, test2;
		char* nibbles = new char[nibbleCount];
		for (i = 0; i < length; i++) {
			inOutMask += 1<<(inOutStart + i);
		}
		otherMask ^= inOutMask;
		std::unique_ptr<Complex16[]> nStateVec(new Complex16[maxQPower]);
		std::fill(&(nStateVec[0]), &(nStateVec[0]) + maxQPower, Complex16(0.0, 0.0));
		for (i = 0; i < maxQPower; i++) {
			otherRes = (i & otherMask);
			partToAdd = toAdd;
			inOutRes = (i & inOutMask);
			inOutInt = inOutRes>>inOutStart;
			isValid = true;
			for (j = 0; j < nibbleCount; j++) {
				test1 = (inOutInt & (15 << (j * 4)))>>(j * 4);
				test2 = (partToAdd % 10);
				partToAdd /= 10;					
				nibbles[j] = test1 + test2;
				if (test1 > 9) {
					isValid = false;
				}
				
			}
			if (isValid) {
				outInt = 0;
				for (j = 0; j < nibbleCount; j++) {
					if (nibbles[j] > 9) {
						nibbles[j] -= 10;
						if ((j + 1) < nibbleCount) {
							nibbles[j + 1]++;
						}
					}
					outInt |= nibbles[j] << (j * 4);
				}
				nStateVec[(outInt<<inOutStart) | otherRes] = stateVec[i];
			}
			else {
				nStateVec[i] = stateVec[i];
			}
		}
		delete [] nibbles;
		stateVec.reset();
		stateVec = std::move(nStateVec);
	}
	///Add BCD integer (without sign, with carry)
	void CoherentUnit::INCBCDC(const bitCapInt toAdd, const bitLenInt inOutStart, const bitLenInt length, const bitLenInt carryIndex) {
		bitCapInt nibbleCount = length / 4;
		if (nibbleCount * 4 != length) {
			throw std::invalid_argument("BCD word bit length must be a multiple of 4.");
		}
		bitCapInt inOutMask = 0;
		bitCapInt carryMask = 1<<carryIndex;
		bitCapInt otherMask = (1<<qubitCount) - 1;
		bitCapInt inOutRes, otherRes, carryRes, outRes, inOutInt, outInt, carryInt, partToAdd, i, j;
		bitCapInt maxMask = 9;
		for (i = 1; i < nibbleCount; i++) {
			maxMask = (maxMask<<4) + 9;
		}
		maxMask <<= inOutStart;
		bool isValid;
		char test1, test2;
		char* nibbles = new char[nibbleCount];
		for (i = 0; i < length; i++) {
			inOutMask += 1<<(inOutStart + i);
		}
		bitCapInt edgeMask = maxMask | carryMask;
		otherMask ^= inOutMask | carryMask;
		std::unique_ptr<Complex16[]> nStateVec(new Complex16[maxQPower]);
		std::fill(&(nStateVec[0]), &(nStateVec[0]) + maxQPower, Complex16(0.0, 0.0));
		for (i = 0; i < maxQPower; i++) {
			otherRes = (i & otherMask);
			if ((edgeMask | i) == i) {
				nStateVec[(i & otherMask) | carryMask] = Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
			else {
				partToAdd = toAdd;
				inOutRes = (i & inOutMask);
				inOutInt = inOutRes>>inOutStart;
				carryInt = (i & carryMask)>>carryIndex;
				isValid = true;

				test1 = (inOutInt & 15);
				test2 = partToAdd % 10;
				partToAdd /= 10;					
				nibbles[0] = test1 + test2 + carryInt;
				if (test1 > 9) {
					isValid = false;
				}

				for (j = 1; j < nibbleCount; j++) {
					test1 = (inOutInt & (15 << (j * 4)))>>(j * 4);
					test2 = partToAdd % 10;
					partToAdd /= 10;					
					nibbles[j] = test1 + test2;
					if (test1 > 9) {
						isValid = false;
					}
					
				}

				if (isValid) {
					outInt = 0;
					carryRes = 0;
					for (j = 0; j < nibbleCount; j++) {
						if (nibbles[j] > 9) {
							nibbles[j] -= 10;
							if ((j + 1) < nibbleCount) {
								nibbles[j + 1]++;
							}
							else {
								carryRes = carryMask;
							}
						}
						outInt |= nibbles[j] << (j * 4);
					}
					outRes = (outInt<<inOutStart) | otherRes | carryRes;
					nStateVec[outRes] += Complex16(norm(stateVec[i]), arg(stateVec[i]));
				}
				else {
					nStateVec[i] = Complex16(norm(stateVec[i]), arg(stateVec[i]));
				}
			}
		}
		for (i = 0; i < maxQPower; i++) {
			nStateVec[i] = polar(sqrt(real(nStateVec[i])), imag(nStateVec[i]));
		}
		delete [] nibbles;
		stateVec.reset();
		stateVec = std::move(nStateVec);
	}
	///Add integer (without sign, with carry)
	void CoherentUnit::INCC(bitCapInt toAdd, bitLenInt inOutStart, bitLenInt length, bitLenInt carryIndex) {
		bitCapInt inOutMask = 0;
		bitCapInt carryMask = 1<<carryIndex;
		bitCapInt otherMask = (1<<qubitCount) - 1;
		bitCapInt lengthPower = 1<<length;
		bitCapInt inOutRes, carryInt, otherRes, outRes, inOutInt, outInt, i;
		for (i = 0; i < length; i++) {
			inOutMask += 1<<(inOutStart + i);
		}
		bitCapInt edgeMask = inOutMask | carryMask;
		otherMask ^= inOutMask | carryMask;
		std::unique_ptr<Complex16[]> nStateVec(new Complex16[maxQPower]);
		std::fill(&(nStateVec[0]), &(nStateVec[0]) + maxQPower, Complex16(0.0, 0.0));
		for (i = 0; i < maxQPower; i++) {
			otherRes = (i & otherMask);
			if ((edgeMask | i) == i) {
				nStateVec[(i & otherMask) | carryMask] = Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
			else {
				inOutRes = (i & inOutMask);
				inOutInt = inOutRes>>inOutStart;
				carryInt = (i & carryMask)>>carryIndex;
				outInt = inOutInt + toAdd + carryInt;
				if (outInt < lengthPower) {
					outRes = (outInt<<inOutStart) | otherRes;
				}
				else {
					outRes = ((outInt - lengthPower)<<inOutStart) | otherRes | carryMask;
				}
				nStateVec[outRes] += Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
		}
		for (i = 0; i < maxQPower; i++) {
			nStateVec[i] = polar(sqrt(real(nStateVec[i])), imag(nStateVec[i]));
		}
		stateVec.reset();
		stateVec = std::move(nStateVec);
	}
	///Add integer (with sign, without carry)
	/** Add an integer to the register, with sign and without carry. Because the register length is an arbitrary number of bits, the sign bit position on the integer to add is variable. Hence, the integer to add is specified as cast to an unsigned format, with the sign bit assumed to be set at the appropriate position before the cast. */
	void CoherentUnit::INCS(bitCapInt toAdd, bitLenInt inOutStart, bitLenInt length, bitLenInt overflowIndex) {
		bitCapInt inOutMask = 0;
		bitCapInt overflowMask = 1<<overflowIndex;
		bitCapInt signMask = (1<<(length - 1));
		bitCapInt otherMask = (1<<qubitCount) - 1;
		bitCapInt lengthPower = 1<<length;
		bitCapInt inOutRes, otherRes, outRes, inOutInt, inInt, outInt, i;
		for (i = 0; i < length; i++) {
			inOutMask += 1<<(inOutStart + i);
		}
		otherMask ^= inOutMask | overflowMask;
		std::unique_ptr<Complex16[]> nStateVec(new Complex16[maxQPower]);
		std::fill(&(nStateVec[0]), &(nStateVec[0]) + maxQPower, Complex16(0.0, 0.0));
		for (i = 0; i < maxQPower; i++) {
			otherRes = (i & otherMask);
			inOutRes = (i & inOutMask);
			inOutInt = inOutRes>>inOutStart;
			inInt = toAdd;
			outInt = inOutInt + toAdd;
			if (outInt < lengthPower) {
				outRes = (outInt<<inOutStart) | otherRes;
			}
			else {
				outRes = ((outInt - lengthPower)<<inOutStart) | otherRes;
			}
			//Both negative:
			if (inOutInt & inInt & signMask) {
				inOutInt = ((~inOutInt) & (lengthPower - 1)) + 1;
				inInt = ((~inInt) & (lengthPower - 1)) + 1;
				if ((inOutInt + inInt) > signMask) outRes |= overflowMask;
			}
			//Both positive:
			else if ((~inOutInt) & (~inInt) & signMask) {
				if ((inOutInt + inInt) >= signMask) outRes |= overflowMask;
			}
			nStateVec[outRes] += Complex16(norm(stateVec[i]), arg(stateVec[i]));
		}
		for (i = 0; i < maxQPower; i++) {
			nStateVec[i] = polar(sqrt(real(nStateVec[i])), imag(nStateVec[i]));
		}
		stateVec.reset();
		stateVec = std::move(nStateVec);
	}
	///Add integer (with sign, with carry)
	/** Add an integer to the register, with sign and with carry. Because the register length is an arbitrary number of bits, the sign bit position on the integer to add is variable. Hence, the integer to add is specified as cast to an unsigned format, with the sign bit assumed to be set at the appropriate position before the cast. */
	void CoherentUnit::INCSC(bitCapInt toAdd, bitLenInt inOutStart, bitLenInt length, bitLenInt overflowIndex, bitLenInt carryIndex) {
		bitCapInt inOutMask = 0;
		bitCapInt overflowMask = 1<<overflowIndex;
		bitCapInt carryMask = 1<<carryIndex;
		bitCapInt otherMask = (1<<qubitCount) - 1;
		bitCapInt lengthPower = 1<<length;
		bitCapInt inOutRes, carryInt, otherRes, outRes, inOutInt, outInt, i;
		for (i = 0; i < length; i++) {
			inOutMask += 1<<(inOutStart + i);
		}
		bitCapInt edgeMask = inOutMask | carryMask;
		otherMask ^= inOutMask | overflowMask | carryMask;
		std::unique_ptr<Complex16[]> nStateVec(new Complex16[maxQPower]);
		std::fill(&(nStateVec[0]), &(nStateVec[0]) + maxQPower, Complex16(0.0, 0.0));
		for (i = 0; i < maxQPower; i++) {
			otherRes = (i & otherMask);
			if ((edgeMask | i) == i) {
				nStateVec[(i & otherMask) | carryMask] += Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
			else {
				inOutRes = (i & inOutMask);
				inOutInt = inOutRes>>inOutStart;
				carryInt = (i & carryMask)>>carryIndex;
				outInt = inOutInt + toAdd + carryInt;
				if (outInt < lengthPower) {
					outRes = (outInt<<inOutStart) | otherRes;
				}
				else {
					outRes = ((outInt - lengthPower)<<inOutStart) | otherRes | carryMask;
				}
				nStateVec[outRes] += Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
		}
		for (i = 0; i < maxQPower; i++) {
			nStateVec[i] = polar(sqrt(real(nStateVec[i])), imag(nStateVec[i]));
		}
		stateVec.reset();
		stateVec = std::move(nStateVec);
	}
	///Subtract integer (without sign)
	void CoherentUnit::DEC(bitCapInt toSub, bitLenInt start, bitLenInt length) {
		bitCapInt lengthPower = 1<<length;
		toSub %= lengthPower;
		if ((length > 0) && (toSub > 0)) {
			bitCapInt i, j;
			bitLenInt end = start + length;
			bitCapInt startPower = 1<<start;
			bitCapInt endPower = 1<<end;
			bitCapInt iterPower = 1<<(qubitCount - end);
			bitCapInt maxLCV = iterPower * endPower;
			for (i = 0; i < startPower; i++) {
				for (j = 0; j < maxLCV; j+=endPower) {
					rotate(&(stateVec[0]) + i + j,
						  &(stateVec[0]) + (toSub * startPower) + i + j,
						  &(stateVec[0]) + endPower + i + j,
						  startPower);
				}
			}
		}
	}
	///Subtract BCD integer (without sign)
	void CoherentUnit::DECBCD(bitCapInt toSub, bitLenInt inOutStart, bitLenInt length) {
		bitCapInt nibbleCount = length / 4;
		if (nibbleCount * 4 != length) {
			throw std::invalid_argument("BCD word bit length must be a multiple of 4.");
		}
		bitCapInt inOutMask = 0;
		bitCapInt otherMask = (1<<qubitCount) - 1;
		bitCapInt inOutRes, otherRes, inOutInt, outInt, partToSub, i, j;
		bool isValid;
		char test1, test2;
		char* nibbles = new char[nibbleCount];
		for (i = 0; i < length; i++) {
			inOutMask += 1<<(inOutStart + i);
		}
		otherMask ^= inOutMask;
		std::unique_ptr<Complex16[]> nStateVec(new Complex16[maxQPower]);
		std::fill(&(nStateVec[0]), &(nStateVec[0]) + maxQPower, Complex16(0.0, 0.0));
		for (i = 0; i < maxQPower; i++) {
			otherRes = (i & otherMask);
			partToSub = toSub;
			inOutRes = (i & inOutMask);
			inOutInt = inOutRes>>inOutStart;
			isValid = true;
			for (j = 0; j < nibbleCount; j++) {
				test1 = (inOutInt & (15 << (j * 4)))>>(j * 4);
				test2 = (partToSub % 10);
				partToSub /= 10;					
				nibbles[j] = test1 - test2;
				if (test1 > 9) {
					isValid = false;
				}
				
			}
			if (isValid) {
				outInt = 0;
				for (j = 0; j < nibbleCount; j++) {
					if (nibbles[j] < 0) {
						nibbles[j] += 10;
						if ((j + 1) < nibbleCount) {
							nibbles[j + 1]--;
						}
					}
					outInt |= nibbles[j] << (j * 4);
				}
				nStateVec[(outInt<<inOutStart) | otherRes] = stateVec[i];
			}
			else {
				nStateVec[i] = stateVec[i];
			}
		}
		delete [] nibbles;
		stateVec.reset();
		stateVec = std::move(nStateVec);
	}
	///Subtract integer (without sign, with carry)
	void CoherentUnit::DECC(const bitCapInt toSub, const bitLenInt inOutStart,  const bitLenInt length, const bitLenInt carryIndex) {
		bitCapInt inOutMask = 0;
		bitCapInt carryMask = 1<<carryIndex;
		bitCapInt otherMask = (1<<qubitCount) - 1;
		bitCapInt lengthPower = 1<<length;
		bitCapInt inOutRes, carryInt, otherRes, outRes, inOutInt, outInt, i;
		for (i = 0; i < length; i++) {
			inOutMask += 1<<(inOutStart + i);
		}
		bitCapInt edgeMask = inOutMask;
		otherMask ^= inOutMask | carryMask;
		std::unique_ptr<Complex16[]> nStateVec(new Complex16[maxQPower]);
		std::fill(&(nStateVec[0]), &(nStateVec[0]) + maxQPower, Complex16(0.0, 0.0));
		for (i = 0; i < maxQPower; i++) {
			otherRes = (i & otherMask);
			if ((((~edgeMask) & i) | carryMask) == i) {
				nStateVec[i | inOutMask] = Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
			else {
				inOutRes = (i & inOutMask);
				inOutInt = inOutRes>>inOutStart;
				carryInt = (i & carryMask)>>carryIndex;
				outInt = (inOutInt - toSub - carryInt) + lengthPower;
				if (outInt < lengthPower) {
					outRes = (outInt << inOutStart) | otherRes | carryMask;
				}
				else {
					outRes = ((outInt - lengthPower) << inOutStart) | otherRes;
				}
				nStateVec[outRes] += Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
		}
		for (i = 0; i < maxQPower; i++) {
			nStateVec[i] = polar(sqrt(real(nStateVec[i])), imag(nStateVec[i]));
		}
		stateVec.reset();
		stateVec = std::move(nStateVec);
	}
	///Subtract integer (with sign, without carry)
	/** Subtract an integer from the register, with sign and without carry. Because the register length is an arbitrary number of bits, the sign bit position on the integer to add is variable. Hence, the integer to add is specified as cast to an unsigned format, with the sign bit assumed to be set at the appropriate position before the cast. */
	void CoherentUnit::DECS(bitCapInt toSub, bitLenInt inOutStart, bitLenInt length, bitLenInt overflowIndex) {
		bitCapInt inOutMask = 0;
		bitCapInt overflowMask = 1<<overflowIndex;
		bitCapInt signMask = 1<<(length - 1);
		bitCapInt otherMask = (1<<qubitCount) - 1;
		bitCapInt lengthPower = 1<<length;
		bitCapInt inOutRes, otherRes, outRes, inOutInt, inInt, outInt, i;
		for (i = 0; i < length; i++) {
			inOutMask += 1<<(inOutStart + i);
		}
		otherMask ^= inOutMask | overflowMask;
		std::unique_ptr<Complex16[]> nStateVec(new Complex16[maxQPower]);
		std::fill(&(nStateVec[0]), &(nStateVec[0]) + maxQPower, Complex16(0.0, 0.0));
		for (i = 0; i < maxQPower; i++) {
			otherRes = (i & otherMask);
			inOutRes = (i & inOutMask);
			inOutInt = inOutRes>>inOutStart;
			inInt = toSub;
			outInt = inOutInt - toSub + lengthPower;
			if (outInt < lengthPower) {
				outRes = (outInt<<inOutStart) | otherRes;
			}
			else {
				outRes = ((outInt - lengthPower)<<inOutStart) | otherRes;
			}
			//First negative:
			if (inOutInt & (~inInt) & signMask) {
				inOutInt = ((~inOutInt) & (lengthPower - 1)) + 1;
				if ((inOutInt + inInt) > signMask) outRes |= overflowMask;
			}
			//First positive:
			else if (inOutInt & (~inInt) & signMask) {
				inInt = ((~inInt) & (lengthPower - 1)) + 1;
				if ((inOutInt + inInt) >= signMask) outRes |= overflowMask;
			}
			nStateVec[outRes] += Complex16(norm(stateVec[i]), arg(stateVec[i]));
		}
		for (i = 0; i < maxQPower; i++) {
			nStateVec[i] = polar(sqrt(real(nStateVec[i])), imag(nStateVec[i]));
		}
		stateVec.reset();
		stateVec = std::move(nStateVec);
	}
	///Add integer (with sign, with carry)
	/** Add an integer to the register, with sign and with carry. Because the register length is an arbitrary number of bits, the sign bit position on the integer to add is variable. Hence, the integer to add is specified as cast to an unsigned format, with the sign bit assumed to be set at the appropriate position before the cast. */
	void CoherentUnit::DECSC(bitCapInt toSub, bitLenInt inOutStart, bitLenInt length, bitLenInt overflowIndex, bitLenInt carryIndex) {
		bitCapInt inOutMask = 0;
		bitCapInt overflowMask = 1<<overflowIndex;
		bitCapInt signMask = 1<<(length - 1);
		bitCapInt carryMask = 1<<carryIndex;
		bitCapInt otherMask = (1<<qubitCount) - 1;
		bitCapInt lengthPower = 1<<length;
		bitCapInt inOutRes, carryInt, otherRes, outRes, inOutInt, inInt, outInt, i;
		for (i = 0; i < length; i++) {
			inOutMask += 1<<(inOutStart + i);
		}
		bitCapInt edgeMask = inOutMask | carryMask;
		otherMask ^= inOutMask | overflowMask | carryMask;
		std::unique_ptr<Complex16[]> nStateVec(new Complex16[maxQPower]);
		std::fill(&(nStateVec[0]), &(nStateVec[0]) + maxQPower, Complex16(0.0, 0.0));
		for (i = 0; i < maxQPower; i++) {
			otherRes = (i & otherMask);
			if ((((~edgeMask) & i) | carryMask) == i) {
				nStateVec[i | inOutMask] = Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
			else {
				inOutRes = (i & inOutMask);
				inOutInt = inOutRes>>inOutStart;
				inInt = toSub;
				carryInt = (i & carryMask)>>carryIndex;
				outInt = inOutInt - toSub - carryInt + lengthPower;
				if (outInt < lengthPower) {
					outRes = (outInt<<inOutStart) | otherRes | carryMask;
				}
				else {
					outRes = ((outInt - lengthPower)<<inOutStart) | otherRes;
				}
				//First negative:
				if (inOutInt & (~inInt) & signMask) {
					inOutInt = ((~inOutInt) & (lengthPower - 1)) + 1;
					if ((inOutInt + inInt - carryInt) > signMask) outRes |= overflowMask;
				}
				//First positive:
				else if (inOutInt & (~inInt) & signMask) {
					inInt = ((~inInt) & (lengthPower - 1)) + 1;
					if ((inOutInt + inInt + carryInt) >= signMask) outRes |= overflowMask;
				}
				nStateVec[outRes] += Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
		}
		for (i = 0; i < maxQPower; i++) {
			nStateVec[i] = polar(sqrt(real(nStateVec[i])), imag(nStateVec[i]));
		}
		stateVec.reset();
		stateVec = std::move(nStateVec);
	}
	void CoherentUnit::DECBCDC(const bitCapInt toSub, const bitLenInt inOutStart, const bitLenInt length, const bitLenInt carryIndex) {
		bitCapInt nibbleCount = length / 4;
		if (nibbleCount * 4 != length) {
			throw std::invalid_argument("BCD word bit length must be a multiple of 4.");
		}
		bitCapInt inOutMask = 0;
		bitCapInt carryMask = 1<<carryIndex;
		bitCapInt otherMask = (1<<qubitCount) - 1;
		bitCapInt inOutRes, otherRes, carryRes, outRes, inOutInt, outInt, carryInt, partToSub, i, j;
		bitCapInt maxMask = 9;
		for (i = 1; i < nibbleCount; i++) {
			maxMask = (maxMask<<4) + 9;
		}
		maxMask <<= inOutStart;
		bool isValid;
		char test1, test2;
		char* nibbles = new char[nibbleCount];
		for (i = 0; i < length; i++) {
			inOutMask += 1<<(inOutStart + i);
		}
		bitCapInt edgeMask = maxMask;
		otherMask ^= inOutMask | carryMask;
		std::unique_ptr<Complex16[]> nStateVec(new Complex16[maxQPower]);
		std::fill(&(nStateVec[0]), &(nStateVec[0]) + maxQPower, Complex16(0.0, 0.0));
		for (i = 0; i < maxQPower; i++) {
			otherRes = (i & otherMask);
			if ((((~edgeMask) & i) | carryMask) == i) {
				nStateVec[i | maxMask] = Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
			else {
				partToSub = toSub;
				inOutRes = (i & inOutMask);
				inOutInt = inOutRes>>inOutStart;
				carryInt = (i & carryMask)>>carryIndex;
				isValid = true;

				test1 = (inOutInt & 15);
				test2 = partToSub % 10;
				partToSub /= 10;					
				nibbles[0] = test1 - test2 - carryInt;
				if (test1 > 9) {
					isValid = false;
				}

				for (j = 1; j < nibbleCount; j++) {
					test1 = (inOutInt & (15 << (j * 4)))>>(j * 4);
					test2 = partToSub % 10;
					partToSub /= 10;					
					nibbles[j] = test1 - test2;
					if (test1 > 9) {
						isValid = false;
					}
					
				}

				if (isValid) {
					outInt = 0;
					carryRes = 0;
					for (j = 0; j < nibbleCount; j++) {
						if (nibbles[j] < 0) {
							nibbles[j] += 10;
							if ((j + 1) < nibbleCount) {
								nibbles[j + 1]--;
							}
							else {
								carryRes = carryMask;
							}
						}
						outInt |= nibbles[j] << (j * 4);
					}
					outRes = (outInt<<inOutStart) | otherRes | carryRes;
					nStateVec[outRes] += Complex16(norm(stateVec[i]), arg(stateVec[i]));
				}
				else {
					nStateVec[i] = Complex16(norm(stateVec[i]), arg(stateVec[i]));
				}
			}
		}
		for (i = 0; i < maxQPower; i++) {
			nStateVec[i] = polar(sqrt(real(nStateVec[i])), imag(nStateVec[i]));
		}
		delete [] nibbles;
		stateVec.reset();
		stateVec = std::move(nStateVec);
	}
	///Add two quantum integers
	/** Add integer of "length" bits in "inStart" to integer of "length" bits in "inOutStart," and store result in "inOutStart." */
	void CoherentUnit::ADD(const bitLenInt inOutStart, const bitLenInt inStart, const bitLenInt length) {
		bitCapInt inOutMask = 0;
		bitCapInt inMask = 0;
		bitCapInt otherMask = (1<<qubitCount) - 1;
		bitCapInt lengthMask = (1<<length) - 1;
		bitCapInt inOutRes, inRes, otherRes, inOutInt, inInt, i;
		for (i = 0; i < length; i++) {
			inOutMask += 1<<(inOutStart + i);
			inMask += 1<<(inStart + i);
		}
		otherMask -= inOutMask + inMask;
		std::unique_ptr<Complex16[]> nStateVec(new Complex16[maxQPower]);
		for (i = 0; i < maxQPower; i++) {
			otherRes = (i & otherMask);
			if (otherRes == i) {
				nStateVec[i] = stateVec[i];
			}
			else {
				inOutRes = (i & inOutMask);
				inOutInt = inOutRes>>inOutStart;
				inRes = (i & inMask);
				inInt = inRes>>inStart;
				nStateVec[(((inOutInt + inInt) & lengthMask)<<inOutStart) | otherRes | inRes] = stateVec[i];
			}
		}
		stateVec.reset(); 
		stateVec = std::move(nStateVec);
	}
	///Add two binary-coded decimal numbers.
	/** Add BCD number of "length" bits in "inStart" to BCD number of "length" bits in "inOutStart," and store result in "inOutStart." */
	void CoherentUnit::ADDBCD(const bitLenInt inOutStart, const bitLenInt inStart, const bitLenInt length) {
		bitCapInt nibbleCount = length / 4;
		if (nibbleCount * 4 != length) {
			throw std::invalid_argument("BCD word bit length must be a multiple of 4.");
		}
		bitCapInt inOutMask = 0;
		bitCapInt inMask = 0;
		bitCapInt otherMask = (1<<qubitCount) - 1;
		bitCapInt inOutRes, inRes, otherRes, inOutInt, inInt, outInt, i, j;
		bool isValid;
		char test1, test2;
		char* nibbles = new char[nibbleCount];
		for (i = 0; i < length; i++) {
			inOutMask += 1<<(inOutStart + i);
			inMask += 1<<(inStart + i);
		}
		otherMask ^= inOutMask + inMask;
		std::unique_ptr<Complex16[]> nStateVec(new Complex16[maxQPower]);
		std::fill(&(nStateVec[0]), &(nStateVec[0]) + maxQPower, Complex16(0.0, 0.0));
		for (i = 0; i < maxQPower; i++) {
			otherRes = (i & otherMask);
			if (otherRes == i) {
				nStateVec[i] = stateVec[i];
			}
			else {
				inOutRes = (i & inOutMask);
				inOutInt = inOutRes>>inOutStart;
				inRes = (i & inMask);
				inInt = inRes>>inStart;
				isValid = true;
				for (j = 0; j < nibbleCount; j++) {
					test1 = (inOutInt & (15 << (j * 4)))>>(j * 4);
					test2 = (inInt & (15 << (j * 4)))>>(j * 4);					
					nibbles[j] = test1 + test2;
					if ((test1 > 9) || (test2 > 9)) {
						isValid = false;
					}
					
				}
				if (isValid) {
					outInt = 0;
					for (j = 0; j < nibbleCount; j++) {
						if (nibbles[j] > 9) {
							nibbles[j] -= 10;
							if ((j + 1) < nibbleCount) {
								nibbles[j + 1]++;
							}
						}
						outInt |= nibbles[j] << (j * 4);
					}
					nStateVec[(outInt<<inOutStart) | otherRes | inRes] = stateVec[i];
				}
				else {
					nStateVec[i] = stateVec[i];
				}
			}
		}
		delete [] nibbles;
		stateVec.reset();
		stateVec = std::move(nStateVec);
	}
	///Add two quantum integers with carry bit
	/** Add integer of "length" bits in "inStart" to integer of "length" bits in "inOutStart," and store result in "inOutStart." Get carry value from bit at "carryIndex" and place end result into this bit. */
	void CoherentUnit::ADDC(const bitLenInt inOutStart, const bitLenInt inStart, const bitLenInt length, const bitLenInt carryIndex) {
		bitCapInt inOutMask = 0;
		bitCapInt inMask = 0;
		bitCapInt carryMask = 1<<carryIndex;
		bitCapInt otherMask = (1<<qubitCount) - 1;
		bitCapInt lengthPower = 1<<length;
		bitCapInt inOutRes, inRes, carryInt, otherRes, outRes, inOutInt, inInt, outInt, i;
		for (i = 0; i < length; i++) {
			inOutMask += 1<<(inOutStart + i);
			inMask += 1<<(inStart + i);
		}
		otherMask ^= inOutMask | inMask | carryMask;
		bitCapInt edgeMask = inOutMask | carryMask | otherMask;
		std::unique_ptr<Complex16[]> nStateVec(new Complex16[maxQPower]);
		std::fill(&(nStateVec[0]), &(nStateVec[0]) + maxQPower, Complex16(0.0, 0.0));
		for (i = 0; i < maxQPower; i++) {
			otherRes = (i & otherMask);
			if (otherRes == i) {
				nStateVec[i] = Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
			else if ((edgeMask & i) == i) {
				nStateVec[(i & otherMask) | carryMask] = Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
			else {
				inOutRes = (i & inOutMask);
				inOutInt = inOutRes>>inOutStart;
				inRes = (i & inMask);
				carryInt = (i & carryMask)>>carryIndex;
				inInt = inRes>>inStart;
				outInt = inOutInt + inInt + carryInt;
				if (outInt < lengthPower) {
					outRes = (outInt<<inOutStart) | otherRes | inRes;
				}
				else {
					outRes = ((outInt - lengthPower)<<inOutStart) | otherRes | inRes | carryMask;
				}
				nStateVec[outRes] += Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
		}
		for (i = 0; i < maxQPower; i++) {
			nStateVec[i] = polar(sqrt(real(nStateVec[i])), imag(nStateVec[i]));
		}
		stateVec.reset();
		stateVec = std::move(nStateVec);
	}
	///Add two signed quantum integers with overflow bit
	/** Add signed integer of "length" bits in "inStart" to signed integer of "length" bits in "inOutStart," and store result in "inOutStart." Set overflow bit when input to output wraps past minimum or maximum integer. */
	void CoherentUnit::ADDS(const bitLenInt inOutStart, const bitLenInt inStart, const bitLenInt length, const bitLenInt overflowIndex) {
		bitCapInt inOutMask = 0;
		bitCapInt inMask = 0;
		bitCapInt overflowMask = 1<<overflowIndex;
		bitCapInt signMask = (1<<(length - 1));
		bitCapInt otherMask = (1<<qubitCount) - 1;
		bitCapInt lengthPower = 1<<length;
		bitCapInt inOutRes, inRes, otherRes, outRes, inOutInt, inInt, outInt, i;
		for (i = 0; i < length; i++) {
			inOutMask += 1<<(inOutStart + i);
			inMask += 1<<(inStart + i);
		}
		otherMask ^= inOutMask | inMask | overflowMask;
		std::unique_ptr<Complex16[]> nStateVec(new Complex16[maxQPower]);
		std::fill(&(nStateVec[0]), &(nStateVec[0]) + maxQPower, Complex16(0.0, 0.0));
		for (i = 0; i < maxQPower; i++) {
			otherRes = (i & otherMask);
			if (otherRes == i) {
				nStateVec[i] = Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
			else {
				inOutRes = (i & inOutMask);
				inOutInt = inOutRes>>inOutStart;
				inRes = (i & inMask);
				inInt = inRes>>inStart;
				outInt = inOutInt + inInt;
				if (outInt < lengthPower) {
					outRes = (outInt<<inOutStart) | otherRes | inRes;
				}
				else {
					outRes = ((outInt - lengthPower)<<inOutStart) | otherRes | inRes;
				}
				//Both negative:
				if (inOutInt & inInt & signMask) {
					inOutInt = ((~inOutInt) & (lengthPower - 1)) + 1;
					inInt = ((~inInt) & (lengthPower - 1)) + 1;
					if ((inOutInt + inInt) > signMask) outRes |= overflowMask;
				}
				//Both positive:
				else if ((~inOutInt) & (~inInt) & signMask) {
					if ((inOutInt + inInt) >= signMask) outRes |= overflowMask;
				}
				nStateVec[outRes] += Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
		}
		for (i = 0; i < maxQPower; i++) {
			nStateVec[i] = polar(sqrt(real(nStateVec[i])), imag(nStateVec[i]));
		}
		stateVec.reset();
		stateVec = std::move(nStateVec);
	}
	///Add two quantum integers with carry bit and overflow bit
	/** Add integer of "length" bits in "inStart" to integer of "length" bits in "inOutStart," and store result in "inOutStart." Get carry value from bit at "carryIndex" and place end result into this bit. Set overflow for signed addition if result wraps past the minimum or maximum signed integer. */
	void CoherentUnit::ADDSC(const bitLenInt inOutStart, const bitLenInt inStart, const bitLenInt length, const bitLenInt overflowIndex, const bitLenInt carryIndex) {
		bitCapInt inOutMask = 0;
		bitCapInt inMask = 0;
		bitCapInt carryMask = 1<<carryIndex;
		bitCapInt overflowMask = 1<<overflowIndex;
		bitCapInt signMask = (1<<(length - 1));
		bitCapInt otherMask = (1<<qubitCount) - 1;
		bitCapInt lengthPower = 1<<length;
		bitCapInt inOutRes, inRes, carryInt, otherRes, outRes, inOutInt, inInt, outInt, i;
		for (i = 0; i < length; i++) {
			inOutMask += 1<<(inOutStart + i);
			inMask += 1<<(inStart + i);
		}
		otherMask ^= inOutMask | inMask | carryMask;
		bitCapInt edgeMask = inOutMask | carryMask | otherMask;
		std::unique_ptr<Complex16[]> nStateVec(new Complex16[maxQPower]);
		std::fill(&(nStateVec[0]), &(nStateVec[0]) + maxQPower, Complex16(0.0, 0.0));
		for (i = 0; i < maxQPower; i++) {
			otherRes = (i & otherMask);
			if (otherRes == i) {
				nStateVec[i] = Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
			else if ((edgeMask & i) == i) {
				nStateVec[(i & otherMask) | carryMask] = Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
			else {
				inOutRes = (i & inOutMask);
				inOutInt = inOutRes>>inOutStart;
				inRes = (i & inMask);
				carryInt = (i & carryMask)>>carryIndex;
				inInt = inRes>>inStart;
				outInt = inOutInt + inInt + carryInt;
				if (outInt < lengthPower) {
					outRes = (outInt<<inOutStart) | otherRes | inRes;
				}
				else {
					outRes = ((outInt - lengthPower)<<inOutStart) | otherRes | inRes | carryMask;
				}
				//Both negative:
				if (inOutInt & inInt & signMask) {
					inOutInt = ((~inOutInt) & (lengthPower - 1)) + 1;
					inInt = ((~inInt) & (lengthPower - 1)) + 1;
					if ((inOutInt + inInt + carryInt) > signMask) outRes |= overflowMask;
				}
				//Both positive:
				else if ((~inOutInt) & (~inInt) & signMask) {
					if ((inOutInt + inInt - carryInt) >= signMask) outRes |= overflowMask;
				}
				nStateVec[outRes] += Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
		}
		for (i = 0; i < maxQPower; i++) {
			nStateVec[i] = polar(sqrt(real(nStateVec[i])), imag(nStateVec[i]));
		}
		stateVec.reset();
		stateVec = std::move(nStateVec);
	}
	///Add two binary-coded decimal numbers.
	/** Add BCD number of "length" bits in "inStart" to BCD number of "length" bits in "inOutStart," and store result in "inOutStart." */
	void CoherentUnit::ADDBCDC(const bitLenInt inOutStart, const bitLenInt inStart, const bitLenInt length, const bitLenInt carryIndex) {
		bitCapInt nibbleCount = length / 4;
		if (nibbleCount * 4 != length) {
			throw std::invalid_argument("BCD word bit length must be a multiple of 4.");
		}
		bitCapInt inOutMask = 0;
		bitCapInt inMask = 0;
		bitCapInt carryMask = 1<<carryIndex;
		bitCapInt otherMask = (1<<qubitCount) - 1;
		bitCapInt inOutRes, inRes, otherRes, carryRes, outRes, inOutInt, inInt, outInt, carryInt, i, j;
		bitCapInt maxMask = 9;
		for (i = 1; i < nibbleCount; i++) {
			maxMask = (maxMask<<4) + 9;
		}
		maxMask <<= inOutStart;
		bool isValid;
		char test1, test2;
		char* nibbles = new char[nibbleCount];
		for (i = 0; i < length; i++) {
			inOutMask += 1<<(inOutStart + i);
			inMask += 1<<(inStart + i);
		}
		otherMask ^= inOutMask | inMask | carryMask;
		bitCapInt edgeMask = maxMask | carryMask | otherMask;
		std::unique_ptr<Complex16[]> nStateVec(new Complex16[maxQPower]);
		std::fill(&(nStateVec[0]), &(nStateVec[0]) + maxQPower, Complex16(0.0, 0.0));
		for (i = 0; i < maxQPower; i++) {
			otherRes = (i & otherMask);
			if (otherRes == i) {
				nStateVec[i] = Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
			else if ((edgeMask & i) == i) {
				nStateVec[(i & otherMask) | carryMask] = Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
			else {
				inOutRes = (i & inOutMask);
				inOutInt = inOutRes>>inOutStart;
				inRes = (i & inMask);
				inInt = inRes>>inStart;
				carryInt = (i & carryMask)>>carryIndex;
				isValid = true;

				test1 = (inOutInt & 15);
				test2 = (inInt & 15);					
				nibbles[0] = test1 + test2 + carryInt;
				if ((test1 > 9) || (test2 > 9)) {
					isValid = false;
				}

				for (j = 1; j < nibbleCount; j++) {
					test1 = (inOutInt & (15 << (j * 4)))>>(j * 4);
					test2 = (inInt & (15 << (j * 4)))>>(j * 4);					
					nibbles[j] = test1 + test2;
					if ((test1 > 9) || (test2 > 9)) {
						isValid = false;
					}
					
				}

				if (isValid) {
					outInt = 0;
					carryRes = 0;
					for (j = 0; j < nibbleCount; j++) {
						if (nibbles[j] > 9) {
							nibbles[j] -= 10;
							if ((j + 1) < nibbleCount) {
								nibbles[j + 1]++;
							}
							else {
								carryRes = carryMask;
							}
						}
						outInt |= nibbles[j] << (j * 4);
					}
					outRes = (outInt<<inOutStart) | otherRes | inRes | carryRes;
					nStateVec[outRes] += Complex16(norm(stateVec[i]), arg(stateVec[i]));
				}
				else {
					nStateVec[i] = Complex16(norm(stateVec[i]), arg(stateVec[i]));
				}
			}
		}
		for (i = 0; i < maxQPower; i++) {
			nStateVec[i] = polar(sqrt(real(nStateVec[i])), imag(nStateVec[i]));
		}
		delete [] nibbles;
		stateVec.reset();
		stateVec = std::move(nStateVec);
	}
	///Subtract two quantum integers
	/** Subtract integer of "length" bits in "toSub" from integer of "length" bits in "inOutStart," and store result in "inOutStart." */
	void CoherentUnit::SUB(const bitLenInt inOutStart, const bitLenInt toSub, const bitLenInt length)  {
		bitCapInt inOutMask = 0;
		bitCapInt inMask = 0;
		bitCapInt otherMask = (1<<qubitCount) - 1;
		bitCapInt lengthPower = (1<<length) - 1;
		bitCapInt inOutRes, inRes, otherRes, inOutInt, inInt, i;
		for (i = 0; i < length; i++) {
			inOutMask += 1<<(inOutStart + i);
			inMask += 1<<(toSub + i);
		}
		otherMask -= inOutMask + inMask;
		std::unique_ptr<Complex16[]> nStateVec(new Complex16[maxQPower]);
		for (i = 0; i < maxQPower; i++) {
			otherRes = (i & otherMask);
			if (otherRes == i) {
				nStateVec[i] = stateVec[i];
			}
			else {
				inOutRes = (i & inOutMask);
				inOutInt = inOutRes>>inOutStart;
				inRes = (i & inMask);
				inInt = inRes>>toSub;
				nStateVec[(((inOutInt - inInt + lengthPower) & lengthPower)<<inOutStart) | otherRes | inRes] = stateVec[i];
			}
		}
		stateVec.reset();
		stateVec = std::move(nStateVec);
	}
	///Subtract two binary-coded decimal numbers.
	/** Subtract BCD number of "length" bits in "inStart" from BCD number of "length" bits in "inOutStart," and store result in "inOutStart." */
	void CoherentUnit::SUBBCD(const bitLenInt inOutStart, const bitLenInt inStart, const bitLenInt length) {
		bitCapInt nibbleCount = length / 4;
		if (nibbleCount * 4 != length) {
			throw std::invalid_argument("BCD word bit length must be a multiple of 4.");
		}
		bitCapInt inOutMask = 0;
		bitCapInt inMask = 0;
		bitCapInt otherMask = (1<<qubitCount) - 1;
		bitCapInt inOutRes, inRes, otherRes, inOutInt, inInt, outInt, i, j;
		bool isValid;
		char test1, test2;
		char* nibbles = new char[nibbleCount];
		for (i = 0; i < length; i++) {
			inOutMask += 1<<(inOutStart + i);
			inMask += 1<<(inStart + i);
		}
		otherMask ^= inOutMask + inMask;
		std::unique_ptr<Complex16[]> nStateVec(new Complex16[maxQPower]);
		std::fill(&(nStateVec[0]), &(nStateVec[0]) + maxQPower, Complex16(0.0, 0.0));
		for (i = 0; i < maxQPower; i++) {
			otherRes = (i & otherMask);
			if (otherRes == i) {
				nStateVec[i] = Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
			else {
				inOutRes = (i & inOutMask);
				inOutInt = inOutRes>>inOutStart;
				inRes = (i & inMask);
				inInt = inRes>>inStart;
				isValid = true;
				for (j = 0; j < nibbleCount; j++) {
					test1 = (inOutInt & (15 << (j * 4)))>>(j * 4);
					test2 = (inInt & (15 << (j * 4)))>>(j * 4);					
					nibbles[j] = test1 - test2;
					if ((test1 > 9) || (test2 > 9)) {
						isValid = false;
					}
					
				}
				if (isValid) {
					outInt = 0;
					for (j = 0; j < nibbleCount; j++) {
						if (nibbles[j] < 0) {
							nibbles[j] += 10;
							if ((j + 1) < nibbleCount) {
								nibbles[j + 1]--;
							}
						}
						outInt |= nibbles[j] << (j * 4);
					}
					nStateVec[(outInt<<inOutStart) | otherRes | inRes] = stateVec[i];
				}
				else {
					nStateVec[i] = Complex16(norm(stateVec[i]), arg(stateVec[i]));
				}
			}
		}
		delete [] nibbles;
		stateVec.reset(); 
		stateVec = std::move(nStateVec);
	}
	///Subtract two quantum integers with carry bit
	/** Subtract integer of "length" - 1 bits in "toSub" from integer of "length" - 1 bits in "inOutStart," and store result in "inOutStart." Get carry value from bit at "carryIndex" and place end result into this bit. */
	void CoherentUnit::SUBC(const bitLenInt inOutStart, const bitLenInt toSub, const bitLenInt length, const bitLenInt carryIndex) {
		bitCapInt inOutMask = 0;
		bitCapInt inMask = 0;
		bitCapInt carryMask = 1<<carryIndex;
		bitCapInt otherMask = (1<<qubitCount) - 1;
		bitCapInt lengthPower = 1<<length;
		bitCapInt inOutRes, inRes, carryInt, otherRes, outRes, inOutInt, inInt, outInt, i;
		for (i = 0; i < length; i++) {
			inOutMask += 1<<(inOutStart + i);
			inMask += 1<<(toSub + i);
		}
		bitCapInt edgeMask = inOutMask | inMask;
		otherMask ^= inOutMask | inMask | carryMask;
		std::unique_ptr<Complex16[]> nStateVec(new Complex16[maxQPower]);
		std::fill(&(nStateVec[0]), &(nStateVec[0]) + maxQPower, Complex16(0.0, 0.0));
		for (i = 0; i < maxQPower; i++) {
			otherRes = (i & otherMask);
			if (otherRes == i) {
				nStateVec[i] = Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
			else if ((((~edgeMask) & i) | carryMask) == i) {
				nStateVec[i | inOutMask] = Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
			else {
				inOutRes = (i & inOutMask);
				inOutInt = inOutRes>>inOutStart;
				inRes = (i & inMask);
				carryInt = (i & carryMask)>>carryIndex;
				inInt = inRes>>toSub;
				outInt = (inOutInt - inInt - carryInt) + lengthPower;
				if (outInt < lengthPower) {
					outRes = (outInt << inOutStart) | otherRes | inRes | carryMask;
				}
				else {
					outRes = ((outInt - lengthPower) << inOutStart) | otherRes | inRes;
				}
				nStateVec[outRes] += Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
		}
		for (i = 0; i < maxQPower; i++) {
			nStateVec[i] = polar(sqrt(real(nStateVec[i])), imag(nStateVec[i]));
		}
		stateVec.reset();
		stateVec = std::move(nStateVec);
	}
	///Subtract two quantum integers with carry bit
	/** Subtract integer of "length" - 1 bits in "toSub" from integer of "length" - 1 bits in "inOutStart," and store result in "inOutStart." Get carry value from bit at "carryIndex" and place end result into this bit. */
	void CoherentUnit::SUBS(const bitLenInt inOutStart, const bitLenInt toSub, const bitLenInt length, const bitLenInt overflowIndex) {
		bitCapInt inOutMask = 0;
		bitCapInt inMask = 0;
		bitCapInt overflowMask = 1<<overflowIndex;
		bitCapInt signMask = 1<<(length - 1);
		bitCapInt otherMask = (1<<qubitCount) - 1;
		bitCapInt lengthPower = 1<<length;
		bitCapInt inOutRes, inRes, otherRes, outRes, inOutInt, inInt, outInt, i;
		for (i = 0; i < length; i++) {
			inOutMask += 1<<(inOutStart + i);
			inMask += 1<<(toSub + i);
		}
		otherMask ^= inOutMask | inMask | overflowMask;
		std::unique_ptr<Complex16[]> nStateVec(new Complex16[maxQPower]);
		std::fill(&(nStateVec[0]), &(nStateVec[0]) + maxQPower, Complex16(0.0, 0.0));
		for (i = 0; i < maxQPower; i++) {
			otherRes = (i & otherMask);
			if (otherRes == i) {
				nStateVec[i] = Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
			else {
				inOutRes = (i & inOutMask);
				inOutInt = inOutRes>>inOutStart;
				inRes = (i & inMask);
				inInt = inRes>>toSub;
				outInt = inOutInt - inInt + lengthPower;
				if (outInt < lengthPower) {
					outRes = (outInt << inOutStart) | otherRes | inRes;
				}
				else {
					outRes = ((outInt - lengthPower) << inOutStart) | otherRes | inRes;
				}
				//First negative:
				if (inOutInt & (~inInt) & signMask) {
					inOutInt = ((~inOutInt) & (lengthPower - 1)) + 1;
					if ((inOutInt + inInt) > signMask) outRes |= overflowMask;
				}
				//First positive:
				else if (inOutInt & (~inInt) & signMask) {
					inInt = ((~inInt) & (lengthPower - 1)) + 1;
					if ((inOutInt + inInt) >= signMask) outRes |= overflowMask;
				}
				nStateVec[outRes] += Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
		}
		for (i = 0; i < maxQPower; i++) {
			nStateVec[i] = polar(sqrt(real(nStateVec[i])), imag(nStateVec[i]));
		}
		stateVec.reset();
		stateVec = std::move(nStateVec);
	}
	///Subtract two quantum integers with carry bit and overflow bit
	/** Subtract integer of "length" bits in "inStart" from integer of "length" bits in "inOutStart," and store result in "inOutStart." Get carry value from bit at "carryIndex" and place end result into this bit. Set overflow for signed addition if result wraps past the minimum or maximum signed integer. */
	void CoherentUnit::SUBSC(const bitLenInt inOutStart, const bitLenInt toSub, const bitLenInt length, const bitLenInt overflowIndex, const bitLenInt carryIndex) {
		bitCapInt inOutMask = 0;
		bitCapInt inMask = 0;
		bitCapInt carryMask = 1<<carryIndex;
		bitCapInt overflowMask = 1<<overflowIndex;
		bitCapInt signMask = 1<<(length - 1);
		bitCapInt otherMask = (1<<qubitCount) - 1;
		bitCapInt lengthPower = 1<<length;
		bitCapInt inOutRes, inRes, carryInt, otherRes, outRes, inOutInt, inInt, outInt, i;
		for (i = 0; i < length; i++) {
			inOutMask += 1<<(inOutStart + i);
			inMask += 1<<(toSub + i);
		}
		bitCapInt edgeMask = inOutMask | inMask;
		otherMask ^= inOutMask | inMask | carryMask;
		std::unique_ptr<Complex16[]> nStateVec(new Complex16[maxQPower]);
		std::fill(&(nStateVec[0]), &(nStateVec[0]) + maxQPower, Complex16(0.0, 0.0));
		for (i = 0; i < maxQPower; i++) {
			otherRes = (i & otherMask);
			if (otherRes == i) {
				nStateVec[i] = Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
			else if ((((~edgeMask) & i) | carryMask) == i) {
				nStateVec[i | inOutMask] = Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
			else {
				inOutRes = (i & inOutMask);
				inOutInt = inOutRes>>inOutStart;
				inRes = (i & inMask);
				carryInt = (i & carryMask)>>carryIndex;
				inInt = inRes>>toSub;
				outInt = (inOutInt - inInt - carryInt) + lengthPower;
				if (outInt < lengthPower) {
					outRes = (outInt << inOutStart) | otherRes | inRes | carryMask;
				}
				else {
					outRes = ((outInt - lengthPower) << inOutStart) | otherRes | inRes;
				}
				//First negative:
				if (inOutInt & (~inInt) & signMask) {
					inOutInt = ((~inOutInt) & (lengthPower - 1)) + 1;
					if ((inOutInt + inInt - carryInt) > signMask) outRes |= overflowMask;
				}
				//First positive:
				else if (inOutInt & (~inInt) & signMask) {
					inInt = ((~inInt) & (lengthPower - 1)) + 1;
					if ((inOutInt + inInt + carryInt) >= signMask) outRes |= overflowMask;
				}
				nStateVec[outRes] += Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
		}
		for (i = 0; i < maxQPower; i++) {
			nStateVec[i] = polar(sqrt(real(nStateVec[i])), imag(nStateVec[i]));
		}
		stateVec.reset();
		stateVec = std::move(nStateVec);
	}
	///Add two binary-coded decimal numbers.
	/** Add BCD number of "length" bits in "inStart" to BCD number of "length" bits in "inOutStart," and store result in "inOutStart." */
	void CoherentUnit::SUBBCDC(const bitLenInt inOutStart, const bitLenInt inStart, const bitLenInt length, const bitLenInt carryIndex) {
		bitCapInt nibbleCount = length / 4;
		if (nibbleCount * 4 != length) {
			throw std::invalid_argument("BCD word bit length must be a multiple of 4.");
		}
		bitCapInt inOutMask = 0;
		bitCapInt inMask = 0;
		bitCapInt carryMask = 1<<carryIndex;
		bitCapInt otherMask = (1<<qubitCount) - 1;
		bitCapInt inOutRes, inRes, otherRes, carryRes, outRes, inOutInt, inInt, outInt, carryInt, i, j;
		bitCapInt maxMask = 9;
		for (i = 1; i < nibbleCount; i++) {
			maxMask = (maxMask<<4) + 9;
		}
		maxMask <<= inOutStart;
		bool isValid;
		char test1, test2;
		char* nibbles = new char[nibbleCount];
		for (i = 0; i < length; i++) {
			inOutMask += 1<<(inOutStart + i);
			inMask += 1<<(inStart + i);
		}
		bitCapInt edgeMask = maxMask | inMask;
		otherMask ^= inOutMask | inMask | carryMask;
		std::unique_ptr<Complex16[]> nStateVec(new Complex16[maxQPower]);
		std::fill(&(nStateVec[0]), &(nStateVec[0]) + maxQPower, Complex16(0.0, 0.0));
		for (i = 0; i < maxQPower; i++) {
			otherRes = (i & otherMask);
			if (otherRes == i) {
				nStateVec[i] = Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
			else if ((((~edgeMask) & i) | carryMask) == i) {
				nStateVec[i | maxMask] = Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
			else {
				inOutRes = (i & inOutMask);
				inOutInt = inOutRes>>inOutStart;
				inRes = (i & inMask);
				inInt = inRes>>inStart;
				carryInt = (i & carryMask)>>carryIndex;
				isValid = true;

				test1 = (inOutInt & 15);
				test2 = (inInt & 15);					
				nibbles[0] = test1 - test2 - carryInt;
				if ((test1 > 9) || (test2 > 9)) {
					isValid = false;
				}

				for (j = 1; j < nibbleCount; j++) {
					test1 = (inOutInt & (15 << (j * 4)))>>(j * 4);
					test2 = (inInt & (15 << (j * 4)))>>(j * 4);					
					nibbles[j] = test1 - test2;
					if ((test1 > 9) || (test2 > 9)) {
						isValid = false;
					}
					
				}

				if (isValid) {
					outInt = 0;
					carryRes = 0;
					for (j = 0; j < nibbleCount; j++) {
						if (nibbles[j] < 0) {
							nibbles[j] += 10;
							if ((j + 1) < nibbleCount) {
								nibbles[j + 1]--;
							}
							else {
								carryRes = carryMask;
							}
						}
						outInt |= nibbles[j] << (j * 4);
					}
					outRes = (outInt<<inOutStart) | otherRes | inRes | carryRes;
					nStateVec[outRes] += Complex16(norm(stateVec[i]), arg(stateVec[i]));
				}
				else {
					nStateVec[i] = Complex16(norm(stateVec[i]), arg(stateVec[i]));
				}
			}
		}
		for (i = 0; i < maxQPower; i++) {
			nStateVec[i] = polar(sqrt(real(nStateVec[i])), imag(nStateVec[i]));
		}
		delete [] nibbles;
		stateVec.reset();
		stateVec = std::move(nStateVec);
	}

	

	/// Quantum Fourier Transform - Apply the quantum Fourier transform to the register
	void CoherentUnit::QFT(bitLenInt start, bitLenInt length) {
		if (length > 0) {
			bitLenInt end = start + length;
			bitLenInt i, j;
			for (i = start; i < end; i++) {
				H(i);
				for (j = 1; j < (end - i); j++) {
					CRTDyad(1, 1<<j, i + j, i); 
				}
			}
		}
	}

	/// For chips with a zero flag, set the zero flag after a register operation.
	void CoherentUnit::SetZeroFlag(bitLenInt start, bitLenInt length, bitLenInt zeroFlag) {
		bitCapInt lengthPower = 1<<length;
		bitCapInt regMask = (lengthPower - 1)<<start;
		bitCapInt flagMask = 1<<zeroFlag;
		bitCapInt otherMask = ((1<<qubitCount) - 1) ^ (regMask | flagMask);
		bitCapInt i;
		std::unique_ptr<Complex16[]> nStateVec(new Complex16[maxQPower]);
		std::fill(&(nStateVec[0]), &(nStateVec[0]) + maxQPower, Complex16(0.0, 0.0));
		for (i = 0; i < maxQPower; i++) {
			if (((i & otherMask) == i) || (((i & otherMask) | flagMask) == i)) {
				nStateVec[i | flagMask] += Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
			else {
				nStateVec[i & (~flagMask)] += Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
		}
		for (i = 0; i < maxQPower; i++) {
			nStateVec[i] = polar(sqrt(real(nStateVec[i])), imag(nStateVec[i]));
		}
		stateVec.reset();
		stateVec = std::move(nStateVec);
	}

	///Set register bits to given permutation
	void CoherentUnit::SetReg(bitLenInt start, bitLenInt length, bitCapInt value) {
		bitCapInt inOutRes = value<<start;
		bitCapInt inOutMask = 0;
		bitCapInt otherMask = (1<<qubitCount) - 1;
		bitCapInt otherRes, outRes, i;
		for (i = 0; i < length; i++) {
			inOutMask += 1<<(start + i);
		}
		otherMask ^= inOutMask;
		std::unique_ptr<Complex16[]> nStateVec(new Complex16[maxQPower]);
		std::fill(&(nStateVec[0]), &(nStateVec[0]) + maxQPower, Complex16(0.0, 0.0));
		for (i = 0; i < maxQPower; i++) {
			otherRes = (i & otherMask);
			outRes = inOutRes | otherRes;
			nStateVec[outRes] += Complex16(norm(stateVec[i]), arg(stateVec[i]));
		}
		for (i = 0; i < maxQPower; i++) {
			nStateVec[i] = polar(sqrt(real(nStateVec[i])), imag(nStateVec[i]));
		}
		stateVec.reset();
		stateVec = std::move(nStateVec);
	}

	///Measure permutation state of a register
	bitCapInt CoherentUnit::MReg(bitLenInt start, bitLenInt length) {
		bitCapInt toRet = 0;
		for (bitLenInt i = 0; i < length; i++) {
			if (M(i + start)) {
				toRet |= 1<<i;
			}
		}
		return toRet;
	}
	///Measure permutation state of an 8 bit register
	unsigned char CoherentUnit::MReg8(bitLenInt start) {
		unsigned char toRet = 0;
		unsigned char power = 1;
		for (bitLenInt i = 0; i < 8; i++) {
			if (M(i + start)) {
				toRet += power;
			}
			power<<=1;
		}
		
		return toRet;
	}

	///Set 8 bit register bits based on read from classical memory
	unsigned char CoherentUnit::SuperposeReg8(bitLenInt inputStart, bitLenInt outputStart, unsigned char* values) {
		std::unique_ptr<Complex16[]> nStateVec(new Complex16[maxQPower]);
		std::fill(&(nStateVec[0]), &(nStateVec[0]) + maxQPower, Complex16(0.0, 0.0));
		bitCapInt inputMask = 255<<inputStart;
		bitCapInt outputMask = 255<<outputStart;
		bitCapInt otherMask = (~inputMask) & (~outputMask);
		bitCapInt inputRes, outputRes, otherRes, regRes, inputInt, outputInt, regInt, i;
		if (inputStart == outputStart) {
			for (i = 0; i < maxQPower; i++) {
				otherRes = i & otherMask;
				regRes = i & inputMask;
				regInt = regRes>>inputStart;
				regInt = values[regInt];
				regRes = regInt<<inputStart;
				nStateVec[regRes | otherRes] += Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
		}
		else {
			for (i = 0; i < maxQPower; i++) {
				otherRes = i & otherMask;
				inputRes = i & inputMask;
				inputInt = inputRes>>inputStart;
				outputInt = values[inputInt];
				outputRes = outputInt<<outputStart;
				nStateVec[outputRes | inputRes | otherRes] += Complex16(norm(stateVec[i]), arg(stateVec[i]));
			}
		}
		double prob, average;
		for (i = 0; i < maxQPower; i++) {
			outputRes = i & outputMask;
			outputInt = outputRes>>outputStart;
			prob = real(nStateVec[i]);
			average += prob * outputInt;
			nStateVec[i] = polar(sqrt(prob), imag(nStateVec[i]));
		}
		stateVec.reset();
		stateVec = std::move(nStateVec);

		return (unsigned char)(average + 0.5);
	}

	//Private CoherentUnit methods
	void CoherentUnit::Apply2x2(bitCapInt offset1, bitCapInt offset2, const Complex16* mtrx,
			const bitLenInt bitCount, const bitCapInt* qPowersSorted, bool doApplyNorm, bool doCalcNorm) {
		Complex16 Y0;
		bitCapInt i, iLow, iHigh, lcv;
		Complex16 nrm = Complex16(doApplyNorm ? (1.0 / runningNorm) : 1.0, 0.0);
		Complex16 qubit[2];
		bitLenInt p;
		lcv = 0;
		i = 0;
		while (i < maxQPower) {				
			qubit[0] = stateVec[i + offset1];
			qubit[1] = stateVec[i + offset2];			
	
			Y0 = qubit[0];
			qubit[0] = nrm * ((mtrx[0] * Y0) + (mtrx[1] * qubit[1]));
			qubit[1] = nrm * ((mtrx[2] * Y0) + (mtrx[3] * qubit[1]));

			stateVec[i + offset1] = qubit[0];
			stateVec[i + offset2] = qubit[1];
	
			lcv++;
			iHigh = lcv;
			i = 0;
			for (p = 0; p < bitCount; p++) {
				iLow = iHigh % qPowersSorted[p];
				i += iLow;
				iHigh = (iHigh - iLow)<<1;			
			}
			i += iHigh;
		}

		if (doCalcNorm) {
			UpdateRunningNorm();
		}
		else {
			runningNorm = 1.0;
		}
	}
	void CoherentUnit::ApplySingleBit(bitLenInt qubitIndex, const Complex16* mtrx, bool doCalcNorm) {
		bitCapInt qPowers[1];
		qPowers[0] = 1<<qubitIndex;
		Apply2x2(qPowers[0], 0, mtrx, 1, qPowers, true, doCalcNorm);
	}
	void CoherentUnit::ApplyControlled2x2(bitLenInt control, bitLenInt target, const Complex16* mtrx, bool doCalcNorm) {
		bitCapInt qPowers[3];
		bitCapInt qPowersSorted[2];
		qPowers[1] = 1 << control;
		qPowers[2] = 1 << target;
		qPowers[0] = qPowers[1] + qPowers[2];
		if (control < target) {
			qPowersSorted[0] = qPowers[1];
			qPowersSorted[1] = qPowers[2];
		}
		else {
			qPowersSorted[0] = qPowers[2];
			qPowersSorted[1] = qPowers[1];
		}
		Apply2x2(qPowers[0], qPowers[1], mtrx, 2, qPowersSorted, false, doCalcNorm);
	}
	void CoherentUnit::ApplyAntiControlled2x2(bitLenInt control, bitLenInt target, const Complex16* mtrx, bool doCalcNorm) {
		bitCapInt qPowers[3];
		bitCapInt qPowersSorted[2];
		qPowers[1] = 1 << control;
		qPowers[2] = 1 << target;
		qPowers[0] = qPowers[1] + qPowers[2];
		if (control < target) {
			qPowersSorted[0] = qPowers[1];
			qPowersSorted[1] = qPowers[2];
		}
		else {
			qPowersSorted[0] = qPowers[2];
			qPowersSorted[1] = qPowers[1];
		}
		Apply2x2(0, qPowers[2], mtrx, 2, qPowersSorted, false, doCalcNorm);
	}
	void CoherentUnit::NormalizeState() {
		bitCapInt lcv;
		for (lcv = 0; lcv < maxQPower; lcv++) {
			stateVec[lcv] /= runningNorm;
		}
		runningNorm = 1.0;
	}
	void CoherentUnit::Reverse(bitLenInt first, bitLenInt last) {
		while ((first < last) && (first < (last - 1))) {
			last--;
			Swap(first, last);
			first++;
		}
	}
	void CoherentUnit::UpdateRunningNorm() {
		bitCapInt lcv;
		double sqrNorm = 0.0;
		for (lcv = 0; lcv < maxQPower; lcv++) {
			sqrNorm += norm(stateVec[lcv]);
		}

		runningNorm = sqrt(sqrNorm);
	}
}
