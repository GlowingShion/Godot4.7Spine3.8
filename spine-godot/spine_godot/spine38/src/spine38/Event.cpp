/******************************************************************************
 * Spine Runtimes License Agreement
 * Last updated January 1, 2020. Replaces all prior versions.
 *
 * Copyright (c) 2013-2020, Esoteric Software LLC
 *
 * Integration of the Spine Runtimes into software or otherwise creating
 * derivative works of the Spine Runtimes is permitted under the terms and
 * conditions of Section 2 of the Spine Editor License Agreement:
 * http://esotericsoftware.com/spine-editor-license
 *
 * Otherwise, it is permitted to integrate the Spine Runtimes into software
 * or otherwise create derivative works of the Spine Runtimes (collectively,
 * "Products"), provided that each user of the Products must obtain their own
 * Spine Editor license and redistribution of the Products in any form must
 * include this license and copyright notice.
 *
 * THE SPINE RUNTIMES ARE PROVIDED BY ESOTERIC SOFTWARE LLC "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL ESOTERIC SOFTWARE LLC BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES,
 * BUSINESS INTERRUPTION, OR LOSS OF USE, DATA, OR PROFITS) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THE SPINE RUNTIMES, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/

#ifdef SPINE_UE4
#include "SpinePluginPrivatePCH.h"
#endif

#include <spine38/Event.h>

#include <spine38/EventData.h>

spine38::Event::Event(float time, const spine38::EventData &data) :
		_data(data),
		_time(time),
		_intValue(0),
		_floatValue(0),
		_stringValue(),
		_volume(1),
		_balance(0) {
}

const spine38::EventData &spine38::Event::getData() {
	return _data;
}

float spine38::Event::getTime() {
	return _time;
}

int spine38::Event::getIntValue() {
	return _intValue;
}

void spine38::Event::setIntValue(int inValue) {
	_intValue = inValue;
}

float spine38::Event::getFloatValue() {
	return _floatValue;
}

void spine38::Event::setFloatValue(float inValue) {
	_floatValue = inValue;
}

const spine38::String &spine38::Event::getStringValue() {
	return _stringValue;
}

void spine38::Event::setStringValue(const spine38::String &inValue) {
	_stringValue = inValue;
}


float spine38::Event::getVolume() {
	return _volume;
}

void spine38::Event::setVolume(float inValue) {
	_volume = inValue;
}

float spine38::Event::getBalance() {
	return _balance;
}

void spine38::Event::setBalance(float inValue) {
	_balance = inValue;
}
