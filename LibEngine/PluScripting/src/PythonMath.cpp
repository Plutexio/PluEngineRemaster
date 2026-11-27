//
// Created by Plutex on 2026-08-11.
//

#include "PluEngine/Scripting/PythonMath.h"

#include <pybind11/operators.h>

#include <sstream>
#include <type_traits>

#include "glm/common.hpp"
#include "glm/geometric.hpp"
#include "glm/matrix.hpp"
#include "glm/trigonometric.hpp"
#include "glm/gtc/quaternion.hpp"

#include "PluEngine/PluTypes.h"

namespace py = pybind11;

namespace
{
	// "Vec3(1, 2, 3.5)" – default ostream formatting, so whole numbers stay short.
	template<typename T>
	std::string FormatComponents(const char* name, const T& value, int count)
	{
		std::ostringstream out;
		out << name << '(';
		for (int index = 0; index < count; ++index)
			out << (index > 0 ? ", " : "") << value[index];
		out << ')';
		return out.str();
	}

	// Keeps the pre-class contract alive: scripts written against the old tuple bindings pass
	// (x, y, z) and still work, because this doubles as the implicit tuple/list conversion.
	template<typename VecT, int Count>
	VecT VectorFromSequence(const py::sequence& values)
	{
		if (py::len(values) != Count)
			throw py::value_error("expected a sequence of " + std::to_string(Count) + " numbers");

		VecT result{};
		for (int index = 0; index < Count; ++index)
			result[index] = values[index].cast<typename VecT::value_type>();
		return result;
	}

	// Everything every vector shares: construction, indexing, iteration, comparison, arithmetic.
	// Per-type constructors and named components are added by the caller.
	template<typename VecT, int Count>
	py::class_<VecT> BindVectorCommon(py::module_& module, const char* name)
	{
		using ComponentT = typename VecT::value_type;

		py::class_<VecT> cls(module, name);

		// Not py::init<>(): glm leaves the default constructor uninitialised unless
		// GLM_FORCE_CTOR_INIT is set, and a Python Vec3() full of garbage would be a trap.
		cls.def(py::init([]() { return VecT(static_cast<ComponentT>(0)); }))
		   .def(py::init<ComponentT>(), py::arg("scalar"))
		   .def(py::init(&VectorFromSequence<VecT, Count>), py::arg("values"))
		   .def("__len__", [](const VecT&) { return Count; })
		   .def("__getitem__", [](const VecT& self, int index)
		   {
			   if (index < 0 || index >= Count)
				   throw py::index_error();
			   return self[index];
		   }, py::arg("index"))
		   .def("__setitem__", [](VecT& self, int index, ComponentT value)
		   {
			   if (index < 0 || index >= Count)
				   throw py::index_error();
			   self[index] = value;
		   }, py::arg("index"), py::arg("value"))
		   .def("__iter__", [](const VecT& self) { return py::make_iterator(&self[0], &self[0] + Count); },
		        py::keep_alive<0, 1>())
		   .def("__repr__", [name](const VecT& self) { return FormatComponents(name, self, Count); })
		   .def(py::self == py::self)
		   .def(py::self != py::self)
		   .def(py::self + py::self)
		   .def(py::self - py::self)
		   .def(py::self * py::self)
		   .def(py::self * ComponentT())
		   .def(ComponentT() * py::self)
		   .def(py::self += py::self)
		   .def(py::self -= py::self)
		   .def(py::self *= ComponentT());

		// No unary minus on unsigned vectors: -UVec2(1, 2) would wrap to (4294967295, 4294967294).
		// Binary subtraction still wraps the same way – that is glm's (and C++'s) unsigned semantics.
		if constexpr (std::is_signed_v<ComponentT>)
			cls.def(-py::self);

		// Division only for float vectors – a component-wise integer divide by zero would take the
		// whole editor down with SIGFPE, and a script has no way to see it coming.
		if constexpr (std::is_floating_point_v<ComponentT>)
		{
			cls.def(py::self / py::self)
			   .def(py::self / ComponentT());
		}

		return cls;
	}

	template<typename VecT>
	void BindVectorMath(py::class_<VecT>& cls)
	{
		cls.def("Length", [](const VecT& self) { return glm::length(self); })
		   .def("LengthSquared", [](const VecT& self) { return glm::dot(self, self); })
		   .def("Normalized", [](const VecT& self) { return glm::normalize(self); })
		   .def("Dot", [](const VecT& self, const VecT& other) { return glm::dot(self, other); }, py::arg("other"))
		   .def("Distance", [](const VecT& self, const VecT& other) { return glm::distance(self, other); },
		        py::arg("other"))
		   .def("Lerp", [](const VecT& self, const VecT& target, float alpha) { return glm::mix(self, target, alpha); },
		        py::arg("target"), py::arg("alpha"));
	}

	// Tuples and lists convert implicitly, so old call sites keep working. Deliberately not
	// py::sequence: str passes PySequence_Check, and converting strings to vectors turns a typo
	// into a confusing runtime error somewhere else.
	template<typename T>
	void AllowSequenceConversion()
	{
		py::implicitly_convertible<py::tuple, T>();
		py::implicitly_convertible<py::list, T>();
	}
}

void Plu::RegisterMathTypes(py::module_& module)
{
	py::class_<Vec2> vec2 = BindVectorCommon<Vec2, 2>(module, "Vec2");
	vec2.def(py::init<float, float>(), py::arg("x"), py::arg("y"))
	    .def_readwrite("x", &Vec2::x)
	    .def_readwrite("y", &Vec2::y);
	BindVectorMath(vec2);
	AllowSequenceConversion<Vec2>();

	py::class_<Vec3> vec3 = BindVectorCommon<Vec3, 3>(module, "Vec3");
	vec3.def(py::init<float, float, float>(), py::arg("x"), py::arg("y"), py::arg("z"))
	    .def_readwrite("x", &Vec3::x)
	    .def_readwrite("y", &Vec3::y)
	    .def_readwrite("z", &Vec3::z)
	    .def("Cross", [](const Vec3& self, const Vec3& other) { return glm::cross(self, other); }, py::arg("other"));
	BindVectorMath(vec3);
	AllowSequenceConversion<Vec3>();

	py::class_<Vec4> vec4 = BindVectorCommon<Vec4, 4>(module, "Vec4");
	vec4.def(py::init<float, float, float, float>(), py::arg("x"), py::arg("y"), py::arg("z"), py::arg("w"))
	    .def_readwrite("x", &Vec4::x)
	    .def_readwrite("y", &Vec4::y)
	    .def_readwrite("z", &Vec4::z)
	    .def_readwrite("w", &Vec4::w);
	BindVectorMath(vec4);
	AllowSequenceConversion<Vec4>();

	py::class_<IVec2> ivec2 = BindVectorCommon<IVec2, 2>(module, "IVec2");
	ivec2.def(py::init<int, int>(), py::arg("x"), py::arg("y"))
	     .def_readwrite("x", &IVec2::x)
	     .def_readwrite("y", &IVec2::y);
	AllowSequenceConversion<IVec2>();

	py::class_<IVec3> ivec3 = BindVectorCommon<IVec3, 3>(module, "IVec3");
	ivec3.def(py::init<int, int, int>(), py::arg("x"), py::arg("y"), py::arg("z"))
	     .def_readwrite("x", &IVec3::x)
	     .def_readwrite("y", &IVec3::y)
	     .def_readwrite("z", &IVec3::z);
	AllowSequenceConversion<IVec3>();

	py::class_<IVec4> ivec4 = BindVectorCommon<IVec4, 4>(module, "IVec4");
	ivec4.def(py::init<int, int, int, int>(), py::arg("x"), py::arg("y"), py::arg("z"), py::arg("w"))
	     .def_readwrite("x", &IVec4::x)
	     .def_readwrite("y", &IVec4::y)
	     .def_readwrite("z", &IVec4::z)
	     .def_readwrite("w", &IVec4::w);
	AllowSequenceConversion<IVec4>();

	// Negative Python ints are rejected (TypeError) on every path into a UVec – constructors,
	// __setitem__, the named components and the tuple/list conversion – rather than wrapped.
	py::class_<UVec2> uvec2 = BindVectorCommon<UVec2, 2>(module, "UVec2");
	uvec2.def(py::init<UInt32, UInt32>(), py::arg("x"), py::arg("y"))
	     .def_readwrite("x", &UVec2::x)
	     .def_readwrite("y", &UVec2::y);
	AllowSequenceConversion<UVec2>();

	py::class_<UVec3> uvec3 = BindVectorCommon<UVec3, 3>(module, "UVec3");
	uvec3.def(py::init<UInt32, UInt32, UInt32>(), py::arg("x"), py::arg("y"), py::arg("z"))
	     .def_readwrite("x", &UVec3::x)
	     .def_readwrite("y", &UVec3::y)
	     .def_readwrite("z", &UVec3::z);
	AllowSequenceConversion<UVec3>();

	py::class_<UVec4> uvec4 = BindVectorCommon<UVec4, 4>(module, "UVec4");
	uvec4.def(py::init<UInt32, UInt32, UInt32, UInt32>(), py::arg("x"), py::arg("y"), py::arg("z"), py::arg("w"))
	     .def_readwrite("x", &UVec4::x)
	     .def_readwrite("y", &UVec4::y)
	     .def_readwrite("z", &UVec4::z)
	     .def_readwrite("w", &UVec4::w);
	AllowSequenceConversion<UVec4>();

	// No __getitem__/__iter__ here on purpose: glm's quaternion component order behind operator[]
	// depends on GLM_FORCE_QUAT_DATA_*, so indexing would silently mean different things across
	// glm configurations. Named components and the explicit (w, x, y, z) order below do not.
	py::class_<Quaternion> quaternion(module, "Quaternion");
	quaternion.def(py::init([]() { return Quaternion(1.0f, 0.0f, 0.0f, 0.0f); }))
	          .def(py::init<float, float, float, float>(), py::arg("w"), py::arg("x"), py::arg("y"), py::arg("z"))
	          .def(py::init([](const py::sequence& values)
	          {
		          if (py::len(values) != 4)
			          throw py::value_error("expected a sequence of 4 numbers (w, x, y, z)");

		          return Quaternion(values[0].cast<float>(), values[1].cast<float>(),
		                            values[2].cast<float>(), values[3].cast<float>());
	          }), py::arg("values"))
	          .def_readwrite("w", &Quaternion::w)
	          .def_readwrite("x", &Quaternion::x)
	          .def_readwrite("y", &Quaternion::y)
	          .def_readwrite("z", &Quaternion::z)
	          .def(py::self == py::self)
	          .def(py::self != py::self)
	          .def(py::self * py::self)
	          .def(py::self * Vec3())
	          .def("Length", [](const Quaternion& self) { return glm::length(self); })
	          .def("Normalized", [](const Quaternion& self) { return glm::normalize(self); })
	          .def("Inverse", [](const Quaternion& self) { return glm::inverse(self); })
	          .def("Slerp", [](const Quaternion& self, const Quaternion& target, float alpha)
	          {
		          return glm::slerp(self, target, alpha);
	          }, py::arg("target"), py::arg("alpha"))
	          // Degrees, pitch=X/yaw=Y/roll=Z – the rotation convention the rest of the engine uses.
	          .def("ToEulerDegrees", [](const Quaternion& self) { return Vec3(glm::degrees(glm::eulerAngles(self))); })
	          .def_static("FromEulerDegrees", [](const Vec3& degrees) { return Quaternion(glm::radians(degrees)); },
	                      py::arg("degrees"))
	          .def("__repr__", [](const Quaternion& self)
	          {
		          std::ostringstream out;
		          out << "Quaternion(" << self.w << ", " << self.x << ", " << self.y << ", " << self.z << ')';
		          return out.str();
	          });
	AllowSequenceConversion<Quaternion>();

	// Column-major, like glm: matrix[0] is the first column, not the first row.
	py::class_<Matrix4> matrix4(module, "Matrix4");
	matrix4.def(py::init([]() { return Matrix4(1.0f); }))
	       .def(py::init<float>(), py::arg("diagonal"))
	       .def_static("Identity", []() { return Matrix4(1.0f); })
	       .def("__len__", [](const Matrix4&) { return 4; })
	       .def("__getitem__", [](const Matrix4& self, int column)
	       {
		       if (column < 0 || column >= 4)
			       throw py::index_error();
		       return self[column];
	       }, py::arg("column"))
	       .def("__setitem__", [](Matrix4& self, int column, const Vec4& value)
	       {
		       if (column < 0 || column >= 4)
			       throw py::index_error();
		       self[column] = value;
	       }, py::arg("column"), py::arg("value"))
	       .def(py::self == py::self)
	       .def(py::self != py::self)
	       .def(py::self * py::self)
	       .def(py::self * Vec4())
	       .def(py::self * float())
	       .def("Inverse", [](const Matrix4& self) { return glm::inverse(self); })
	       .def("Transposed", [](const Matrix4& self) { return glm::transpose(self); })
	       .def("__repr__", [](const Matrix4& self)
	       {
		       std::ostringstream out;
		       out << "Matrix4(";
		       for (int column = 0; column < 4; ++column)
			       out << (column > 0 ? ", " : "") << FormatComponents("", self[column], 4);
		       out << ')';
		       return out.str();
	       });
}
