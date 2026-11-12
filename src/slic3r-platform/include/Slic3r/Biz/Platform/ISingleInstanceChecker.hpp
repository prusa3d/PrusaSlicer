#pragma once


namespace Slic3r::Biz::Platform {

class ISingleInstanceChecker
{
public:
	virtual ~ISingleInstanceChecker() = default;

	virtual bool is_another_running() = 0;
	virtual bool is_primary_instance() = 0;
};
}