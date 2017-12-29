/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2017 Cord Harms

 This file is part of QuantLib, a free-software/open-source library
 for financial quantitative analysts and developers - http://quantlib.org/

 QuantLib is free software: you can redistribute it and/or modify it
 under the terms of the QuantLib license.  You should have received a
 copy of the license along with this program; if not, please email
 <quantlib-dev@lists.sf.net>. The license is also available online at
 <http://quantlib.org/license.shtml>.

 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE.  See the license for more details.
*/

#include <ql\experimental\termstructures\localcorrsurfaceabf.hpp>

namespace QuantLib {

    LocalCorrSurfaceABF::LocalCorrSurfaceABF(
		const std::vector<boost::shared_ptr<QuantLib::GeneralizedBlackScholesProcess>>& processes,
		const boost::shared_ptr<QuantLib::GeneralizedBlackScholesProcess>&			    processToCal)
    : LocalCorrTermStructure(processes, processToCal){
      
    }

    void LocalCorrSurfaceABF::accept(AcyclicVisitor& v) {
        Visitor<LocalCorrSurfaceABF>* v1 =
            dynamic_cast<Visitor<LocalCorrSurfaceABF>*>(&v);
        if (v1 != 0)
            v1->visit(*this);
        else
            LocalCorrTermStructure::accept(v);
    }

	void LocalCorrSurfaceABF::localCorrImpl(RealStochasticProcess::MatA& corrMatrix, Time t, const RealStochasticProcess::VecA& X0,
		bool extrapolate)
		const {
		Real lambda = (localF(t,X0, extrapolate)-localA(t,X0, extrapolate))/localB(t,X0, extrapolate);
		for (size_t i = 0; i < corrMatrix.size(); i++)
		{
			for (size_t j = i; j < corrMatrix[i].size(); j++)
			{
				corrMatrix[i][j] = (1 - lambda)*corr0_[i][j] + lambda  *corr1_[i][j];
				QL_REQUIRE(corrMatrix[i][j] != 1 || i==j, "correlation is not allowed wo be 1 for i!=j");
				corrMatrix[j][i] = corrMatrix[i][j];
			}
		}
	}
}

