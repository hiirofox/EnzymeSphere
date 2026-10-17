template<typename R, typename... A> R __enzyme_autodiff(void*, A...);
float loss(float x) { return x*x; }
float gradient(float x) { return __enzyme_autodiff<float>((void*)loss, x); }
