#include "NuclearData.hh"
#include "MC_RNG_State.hh"
#include "DeclareMacro.hh"


// Set the cross section values and reaction type
// Also integrates the cross section and scales to a specified reaction
// cross section.
NuclearDataReaction::NuclearDataReaction(
   Enum reactionType, double nuBar, const qs_vector<double>& energies,
   const Polynomial& polynomial, double reactionCrossSection)
: _crossSection(energies.size()-1, 0., VAR_MEM),
  _reactionType(reactionType),
  _nuBar(nuBar)
{
   int nGroups = _crossSection.size();

   for (int ii=0; ii<nGroups; ++ii)
   {
      double energy = (energies[ii] + energies[ii+1]) / 2.0;
      _crossSection[ii] = polynomial(energy);
   }

   //integrate reaction cross section
   double integral = 0.0;
   for (int ii = 0; ii < nGroups; ii++)
   {
      double binWidth = energies[ii+1] - energies[ii];
      integral += _crossSection[ii] * binWidth;
   }
   integral /= (energies[nGroups] - energies[0]);

   // scale to specified reaction cross section
   double scale = reactionCrossSection/integral;
   for (int ii=0; ii<nGroups; ++ii)
      _crossSection[ii] *= scale;
}

//This has problems as written for GPU code so replaced vectors with arrays
#if 0
// Sample the collision
void NuclearDataReaction::sampleCollision(
   double incidentEnergy, qs_vector<double> &energyOut,
   qs_vector<double> &angleOut, uint64_t* seed)
#endif

HOST_DEVICE

void NuclearDataReaction::sampleCollision(
   double incidentEnergy, double material_mass, double* energyOut,
   double* angleOut, int *energy_angle_size, uint64_t* seed)
{
   double randomNumber;
   switch(_reactionType)
   {
     case Scatter:
      energy_angle_size[0] = 1;
      randomNumber = rngSample(seed);
      energyOut[0] = incidentEnergy * (1.0 - (randomNumber*(1.0/material_mass)));
      randomNumber = rngSample(seed) * 2.0 - 1.0;
      angleOut[0] = randomNumber;
      break;
     case Absorption:
      break;
     case Fission:
      {
         int numParticleOut = (int)(_nuBar + rngSample(seed));
         energy_angle_size[0] = numParticleOut;
         for (int outIndex = 0; outIndex < numParticleOut; outIndex++)
         {
            randomNumber = rngSample(seed) / 2.0 + 0.5;
            energyOut[outIndex] = (20 * randomNumber*randomNumber);
            randomNumber = rngSample(seed) * 2.0 - 1.0;
            angleOut[outIndex] = randomNumber;
         }
      }
      break;
     case Undefined:
      printf("_reactionType invalid\n");
      qs_assert(false);
   }
}

HOST_DEVICE_END

// Then call this for each reaction to set cross section values
void NuclearDataSpecies::addReaction(
   NuclearDataReaction::Enum type, double nuBar,
   qs_vector<double> &energies, const Polynomial& polynomial, double reactionCrossSection)
{
   _reactions.Open();
   _reactions.push_back(NuclearDataReaction(type, nuBar, energies, polynomial, reactionCrossSection));
   _reactions.Close();
}



// Set up the energies boundaries of the neutron
NuclearData::NuclearData(int numGroups, double energyLow, double energyHigh) : _energies( numGroups+1,VAR_MEM)
{
   qs_assert (energyLow < energyHigh);
   _energies[0] = energyLow;
   _energies[numGroups] = energyHigh;
   double logLow = log(energyLow);
   double logHigh = log(energyHigh);
   double delta = (logHigh - logLow) / (numGroups + 1.0);
   for (int energyIndex = 1; energyIndex < numGroups; energyIndex++)
   {
      double logValue = logLow + delta *energyIndex;
      _energies[energyIndex] = exp(logValue);
   }
}


int NuclearData::addIsotope(
   int nReactions,
   const Polynomial& fissionFunction,
   const Polynomial& scatterFunction,
   const Polynomial& absorptionFunction,
   double nuBar,
   double totalCrossSection,
   double fissionWeight, double scatterWeight, double absorptionWeight)
{
   _isotopes.Open();
   _isotopes.push_back(NuclearDataIsotope());
   _isotopes.Close();

   double totalWeight = fissionWeight + scatterWeight + absorptionWeight;

   int nFission    = nReactions / 3;
   int nScatter    = nReactions / 3;
   int nAbsorption = nReactions / 3;
   switch (nReactions % 3)
   {
     case 0:
      break;
     case 1:
      ++nScatter;
      break;
     case 2:
      ++nScatter;
      ++nFission;
      break;
   }
   
   double fissionCrossSection    = (totalCrossSection * fissionWeight)    / (nFission    * totalWeight);
   double scatterCrossSection    = (totalCrossSection * scatterWeight)    / (nScatter    * totalWeight);
   double absorptionCrossSection = (totalCrossSection * absorptionWeight) / (nAbsorption * totalWeight);

   _isotopes.back()._species[0]._reactions.reserve( nReactions, VAR_MEM);

   for (int ii=0; ii<nReactions; ++ii)
   {
      NuclearDataReaction::Enum type;
      Polynomial polynomial(0.0, 0.0, 0.0, 0.0, 0.0);
      double reactionCrossSection = 0.;
      // reaction index % 3 is one of the 3 reaction types
      switch (ii % 3)
      {
        case 0:
         type = NuclearDataReaction::Scatter;
         polynomial = scatterFunction;
         reactionCrossSection = scatterCrossSection;
         break;
        case 1:
         type = NuclearDataReaction::Fission;
         polynomial = fissionFunction;
         reactionCrossSection = fissionCrossSection;
         break;
        case 2:
         type = NuclearDataReaction::Absorption;
         polynomial = absorptionFunction;
         reactionCrossSection = absorptionCrossSection;
         break;
      }
      _isotopes.back()._species[0].addReaction(type, nuBar, _energies, polynomial, reactionCrossSection);
   }
   

   return _isotopes.size() - 1;
}

HOST_DEVICE
// Return the cross section for this energy group
double NuclearDataReaction::getCrossSection(unsigned int group)
{
   qs_assert(group < _crossSection.size());
   return _crossSection[group];
}
HOST_DEVICE_END

HOST_DEVICE
int NuclearData::getNumberReactions(unsigned int isotopeIndex)
{
   qs_assert(isotopeIndex < _isotopes.size());
   return (int)_isotopes[isotopeIndex]._species[0]._reactions.size();
}
HOST_DEVICE_END

// For this energy, return the group index for a neutron
HOST_DEVICE
int NuclearData::getEnergyGroup(double energy)
{
   int numEnergies = (int)_energies.size();
   if (energy <= _energies[0]) return 0;
   if (energy > _energies[numEnergies-1]) return numEnergies-1;

   for( int index = 1; index < numEnergies; index++ )
   {
       if( energy < _energies[index] )
           return index-1;
   }

   return 0;
}
HOST_DEVICE_END

// General routines to help access data lower down
// Return the total cross section for this energy group
HOST_DEVICE
double NuclearData::getTotalCrossSection(unsigned int isotopeIndex, unsigned int group)
{
   qs_assert(isotopeIndex < _isotopes.size());
   int numReacts = (int)_isotopes[isotopeIndex]._species[0]._reactions.size();
   double totalCrossSection = 0.0;
   for (int reactIndex = 0; reactIndex < numReacts; reactIndex++)
   {
      totalCrossSection += _isotopes[isotopeIndex]._species[0]._reactions[reactIndex].getCrossSection(group);
   }
   return totalCrossSection;
}
HOST_DEVICE_END

// Return the total cross section for this energy group
HOST_DEVICE
double NuclearData::getReactionCrossSection(
   unsigned int reactIndex, unsigned int isotopeIndex, unsigned int group)
{
   qs_assert(isotopeIndex < _isotopes.size());
   qs_assert(reactIndex < _isotopes[isotopeIndex]._species[0]._reactions.size());
   return _isotopes[isotopeIndex]._species[0]._reactions[reactIndex].getCrossSection(group);
}
HOST_DEVICE_END

