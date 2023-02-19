/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2006 Allen Kuo

 This file is part of QuantLib, a free-software/open-source library
 for financial quantitative analysts and developers - http://quantlib.org/

 QuantLib is free software: you can redistribute it and/or modify it
 under the terms of the QuantLib license. You should have received a
 copy of the license along with this program; if not, please email
 <quantlib-dev@lists.sf.net>. The license is also available online at
 <http://quantlib.org/license.shtml>.

 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE.  See the license for more details.
*/

#include <ql/event.hpp>
#include <ql/indexes/iborindex.hpp>
#include <ql/instruments/forwardrateagreement.hpp>
#include <utility>
#include <iostream>

namespace QuantLib {

    ForwardRateAgreement::ForwardRateAgreement(const Date& valueDate,
                                               const Date& maturityDate,
                                               Position::Type type,
                                               Rate strikeForwardRate,
                                               Real notionalAmount,
                                               const ext::shared_ptr<IborIndex>& index,
                                               Handle<YieldTermStructure> discountCurve,
                                               bool useIndexedCoupon)
    : fraType_(type), notionalAmount_(notionalAmount), index_(index),
      useIndexedCoupon_(useIndexedCoupon), dayCounter_(index->dayCounter()),
      calendar_(index->fixingCalendar()), businessDayConvention_(index->businessDayConvention()),
      valueDate_(valueDate), maturityDate_(maturityDate),
      discountCurve_(std::move(discountCurve)) {

        maturityDate_ = calendar_.adjust(maturityDate_, businessDayConvention_);

        registerWith(Settings::instance().evaluationDate());
        registerWith(discountCurve_);

        QL_REQUIRE(notionalAmount > 0.0, "notionalAmount must be positive");
        QL_REQUIRE(valueDate_ < maturityDate_, "valueDate must be earlier than maturityDate");

        strikeForwardRate_ = InterestRate(strikeForwardRate,
                                          index->dayCounter(),
                                          Simple, Once);
        registerWith(index_);
    }

    ForwardRateAgreement::ForwardRateAgreement(const Date& valueDate,
                                               Position::Type type,
                                               Rate strikeForwardRate,
                                               Real notionalAmount,
                                               const ext::shared_ptr<IborIndex>& index,
                                               Handle<YieldTermStructure> discountCurve,
                                               bool useIndexedCoupon)
    : ForwardRateAgreement(valueDate, index->maturityDate(valueDate), type, strikeForwardRate,
                           notionalAmount, index, std::move(discountCurve), useIndexedCoupon) {}

    ForwardRateAgreement::ForwardRateAgreement(const Date& valueDate,
                                               const Date& maturityDate,
                                               Position::Type type,
                                               Rate strikeForwardRate,
                                               Real notionalAmount,
                                               Handle<YieldTermStructure> discountCurve,
                                               const Real fixingDays,
                                               const BusinessDayConvention businessDayConvention)
    : fraType_(type), notionalAmount_(notionalAmount),
      useIndexedCoupon_(false), dayCounter_(discountCurve->dayCounter()),
      calendar_(discountCurve->calendar()), businessDayConvention_(businessDayConvention),
      valueDate_(valueDate), maturityDate_(maturityDate),
      discountCurve_(std::move(discountCurve)), fixingDays_(fixingDays) {

        QL_REQUIRE(notionalAmount > 0.0, "notionalAmount must be positive");

        registerWith(Settings::instance().evaluationDate());
        registerWith(discountCurve_);
        
        maturityDate_ = calendar_.adjust(maturityDate_, businessDayConvention_);
        QL_REQUIRE(valueDate_ < maturityDate_, "valueDate must be earlier than maturityDate");
        strikeForwardRate_ = InterestRate(strikeForwardRate,
                                          discountCurve_->dayCounter(),
                                          Simple, Once);
    }

    Date ForwardRateAgreement::fixingDate() const {
        if (index_)
            return index_->fixingDate(valueDate_);
        else {
            Date fixingDate_ = calendar_.advance(valueDate_,
                -static_cast<Integer>(fixingDays_), Days, businessDayConvention_);
            return fixingDate_;
        }
    }

    bool ForwardRateAgreement::isExpired() const {
        return detail::simple_event(valueDate_).hasOccurred();
    }

    Real ForwardRateAgreement::amount() const {
        calculate();
        return amount_;
    }

    InterestRate ForwardRateAgreement::forwardRate() const {
        calculate();
        return forwardRate_;
    }

    void ForwardRateAgreement::setupExpired() const {
        Instrument::setupExpired();

        calculateForwardRate();
    }

    void ForwardRateAgreement::performCalculations() const {
        calculateAmount();

        Handle<YieldTermStructure> discount =
            discountCurve_.empty() ? index_->forwardingTermStructure() : discountCurve_;

        NPV_ = amount_ * discount->discount(valueDate_);
    }

    void ForwardRateAgreement::calculateForwardRate() const {
        if (useIndexedCoupon_ && index_)
            forwardRate_ =
                InterestRate(index_->fixing(fixingDate()), index_->dayCounter(), Simple, Once);
        else if (!useIndexedCoupon_ && index_)
            // par coupon approximation
            forwardRate_ =
                InterestRate((index_->forwardingTermStructure()->discount(valueDate_) /
                                  index_->forwardingTermStructure()->discount(maturityDate_) -
                              1.0) /
                                 index_->dayCounter().yearFraction(valueDate_, maturityDate_),
                             index_->dayCounter(), Simple, Once);
        else // calculating forward rate using the term structure
            forwardRate_ =
                InterestRate((discountCurve_->discount(valueDate_) /
                              discountCurve_->discount(maturityDate_) -
                              1.0) /
                              discountCurve_->dayCounter().yearFraction(valueDate_, maturityDate_),
                              discountCurve_->dayCounter(), Simple, Once);
                          
    }

    void ForwardRateAgreement::calculateAmount() const {
        calculateForwardRate();
        Integer sign = fraType_ == Position::Long? 1 : -1;

        Rate F = forwardRate_.rate();
        Rate K = strikeForwardRate_.rate();
        Time T = forwardRate_.dayCounter().yearFraction(valueDate_, maturityDate_);

        amount_ = notionalAmount_ * sign * (F - K) * T / (1.0 + F * T);
    }

}
