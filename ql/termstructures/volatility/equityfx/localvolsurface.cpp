/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2003 Ferdinando Ametrano

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

#include <ql/termstructures/volatility/equityfx/localvolsurface.hpp>
#include <ql/termstructures/volatility/equityfx/blackvoltermstructure.hpp>
#include <ql/termstructures/yieldtermstructure.hpp>
#include <ql/quotes/simplequote.hpp>
#include <ql/math/interpolations/cubicinterpolation.hpp>
#include <boost/make_shared.hpp>
#include <ql/timegrid.hpp>

namespace QuantLib {


    const Date& LocalVolSurface::referenceDate() const {
        return blackTS_->referenceDate();
    }

    DayCounter LocalVolSurface::dayCounter() const {
        return blackTS_->dayCounter();
    }

    Date LocalVolSurface::maxDate() const {
        return blackTS_->maxDate();
    }

    Real LocalVolSurface::minStrike() const {
        return blackTS_->minStrike();
    }

    Real LocalVolSurface::maxStrike() const {
        return blackTS_->maxStrike();
    }

    LocalVolSurface::LocalVolSurface(
                                 const Handle<BlackVolTermStructure>& blackTS,
                                 const Handle<YieldTermStructure>& riskFreeTS,
                                 const Handle<YieldTermStructure>& dividendTS,
                                 const Handle<Quote>& underlying)
    : LocalVolTermStructure(blackTS->businessDayConvention(),
                            blackTS->dayCounter()),
      blackTS_(blackTS), riskFreeTS_(riskFreeTS), dividendTS_(dividendTS),
      underlying_(underlying) {
        registerWith(blackTS_);
        registerWith(riskFreeTS_);
        registerWith(dividendTS_);
        registerWith(underlying_);
    }

    LocalVolSurface::LocalVolSurface(
                                 const Handle<BlackVolTermStructure>& blackTS,
                                 const Handle<YieldTermStructure>& riskFreeTS,
                                 const Handle<YieldTermStructure>& dividendTS,
                                 Real underlying)
    : LocalVolTermStructure(blackTS->businessDayConvention(),
                            blackTS->dayCounter()),
      blackTS_(blackTS), riskFreeTS_(riskFreeTS), dividendTS_(dividendTS),
      underlying_(ext::shared_ptr<Quote>(new SimpleQuote(underlying))) {
        registerWith(blackTS_);
        registerWith(riskFreeTS_);
        registerWith(dividendTS_);
    }

    void LocalVolSurface::accept(AcyclicVisitor& v) {
        Visitor<LocalVolSurface>* v1 =
            dynamic_cast<Visitor<LocalVolSurface>*>(&v);
        if (v1 != 0)
            v1->visit(*this);
        else
            LocalVolTermStructure::accept(v);
    }

    Volatility LocalVolSurface::localVolImpl(Time t, Real underlyingLevel)
                                                                     const {

        DiscountFactor dr = riskFreeTS_->discount(t, true);
        DiscountFactor dq = dividendTS_->discount(t, true);
        Real forwardValue = underlying_->value()*dq/dr;
        
        // strike derivatives
        Real strike, y, dy, strikep, strikem;
        Real w, wp, wm, dwdy, d2wdy2;
        strike = underlyingLevel;

		//otherwise variance may decrease through extrapolation, temporarily constant extrapolation
		if (strike > blackTS_->maxStrike()) strike = blackTS_->maxStrike();
		if (strike < blackTS_->minStrike()) strike = blackTS_->minStrike();

        y = std::log(strike/forwardValue);
        dy = ((std::fabs(y) > 0.001) ? y*0.0001 : 0.0001); //change: 0.000001 due to errors non-convexity for close to atm
        strikep=strike*std::exp(dy);
        strikem=strike/std::exp(dy);
        w  = blackTS_->blackVariance(t, strike,  true);
        wp = blackTS_->blackVariance(t, strikep, true);
        wm = blackTS_->blackVariance(t, strikem, true);
        dwdy = (wp-wm)/(2.0*dy);
        d2wdy2 = (wp-2.0*w+wm)/(dy*dy);

        // time derivative
        Real dt, wpt, wmt, dwdt;
        if (t==0.0) {
            dt = 0.0001;
            DiscountFactor drpt = riskFreeTS_->discount(t+dt, true);
            DiscountFactor dqpt = dividendTS_->discount(t+dt, true);           
            Real strikept = strike*dr*dqpt/(drpt*dq);
        
            wpt = blackTS_->blackVariance(t+dt, strikept, true);
            QL_ENSURE(wpt>=w,
                      "decreasing variance at strike " << strike
                      << " between time " << t << " and time " << t+dt);
            dwdt = (wpt-w)/dt;
        } else {
            dt = std::min<Time>(0.0001, t/2.0);
            DiscountFactor drpt = riskFreeTS_->discount(t+dt, true);
            DiscountFactor drmt = riskFreeTS_->discount(t-dt, true);
            DiscountFactor dqpt = dividendTS_->discount(t+dt, true);
            DiscountFactor dqmt = dividendTS_->discount(t-dt, true);
            
            Real strikept = strike*dr*dqpt/(drpt*dq);
            Real strikemt = strike*dr*dqmt/(drmt*dq);
            
            wpt = blackTS_->blackVariance(t+dt, strikept, true);
            wmt = blackTS_->blackVariance(t-dt, strikemt, true);

            QL_ENSURE(wpt>=w,
                      "decreasing variance at strike " << strike
                      << " between time " << t << " and time " << t+dt);
            QL_ENSURE(w>=wmt,
                      "decreasing variance at strike " << strike
                      << " between time " << t-dt << " and time " << t);
         
            dwdt = (wpt-wmt)/(2.0*dt);
        }

        if (dwdy==0.0 && d2wdy2==0.0) { // avoid /w where w might be 0.0
            return std::sqrt(dwdt);
        } else {
            Real den1 = 1.0 - y/w*dwdy;
            Real den2 = 0.25*(-0.25 - 1.0/w + y*y/w/w)*dwdy*dwdy;
            Real den3 = 0.5*d2wdy2;
            Real den = den1+den2+den3;
            Real result = dwdt / den;

			if (result < 0.0) {
				result = result*1.1;
			}

            QL_ENSURE(result>=0.0,
                      "negative local vol^2 at strike " << strike
                      << " and time " << t
                      << "; the black vol surface is not smooth enough ( dwdt:" << dwdt << ", w:" << w
					  << ", y:" << y << ", dwdy:" << dwdy << ", d2wdy2:" << d2wdy2 << ")");
			
            return std::sqrt(result);
        }
    }

	InterpolatedLocalVolSurface::InterpolatedLocalVolSurface(
		const Handle<BlackVolTermStructure>& blackTS,
		const Handle<YieldTermStructure>& riskFreeTS,
		const Handle<YieldTermStructure>& dividendTS,
		const Handle<Quote>& underlying, Size strikeGridAmt, Size timeStepsPerYear)
		: LocalVolSurface(blackTS,riskFreeTS,dividendTS,underlying)
	{
		
		std::vector<Time> gridTimes;
		gridTimes.push_back(blackTS->dayCounter().yearFraction(blackTS->referenceDate(), blackTS->maxDate()));

		boost::shared_ptr<TimeGrid> timeGrid;
		timeGrid = boost::make_shared<TimeGrid>(gridTimes.begin(), gridTimes.end(),
			std::max(Size(2), Size(gridTimes.back()*timeStepsPerYear)));
		
		Size timeGridAmt = timeGrid->size();


		const boost::shared_ptr<Matrix> localVolMatrix(new Matrix(strikeGridAmt, timeGridAmt));

		strikes_ = std::vector<boost::shared_ptr<std::vector<Real> > >(timeGridAmt);

		Real ds;

		ds = (maxStrike() - minStrike()) / strikeGridAmt;

		for (size_t i = 0; i < timeGridAmt; i++)
		{
			strikes_[i] = boost::make_shared<std::vector<Real> >(strikeGridAmt);
			strikes_[i]->at(0) = minStrike();
			(*localVolMatrix)[0][i] = LocalVolSurface::localVolImpl(timeGrid->at(i), strikes_[i]->at(0));
			for (size_t j = 1; j < strikeGridAmt; j++)
			{
				strikes_[i]->at(j) = strikes_[i]->at(j-1) + ds;
				(*localVolMatrix)[j][i] = LocalVolSurface::localVolImpl(timeGrid->at(i),strikes_[i]->at(j));
			}
		}

		gridTimes_ = std::vector<Time>(timeGrid->begin(), timeGrid->end());
		
		surface_ = boost::make_shared<FixedLocalVolSurface>(blackTS->referenceDate(), 
			gridTimes_, strikes_, localVolMatrix, blackTS->dayCounter());
		surface_->setInterpolation<Cubic>(); //Extrapolation will still be constant in strike, see localVolImpl
	}

	Volatility InterpolatedLocalVolSurface::localVolImpl(Time t, Real strike) const {
		if (strike > maxStrike())strike = maxStrike();
		else if (strike < minStrike()) strike = minStrike();
		return surface_->localVol(t, strike,true);
	}

}

