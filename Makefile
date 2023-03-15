PYTHON = python3


help:
	@echo "-------------------------HELP---------------------------"
	@echo "make install - compile binaries and build documentation"
	@echo "make clean   - remove compiled elements/autodoc stubs"
	@echo "--------------------------------------------------------"


install:
	@${PYTHON} setup.py build_ext --inplace
	@cd docs/ && $(MAKE) html


clean:
	@find pdcast/ -name "*.c" -type f -delete
	@find pdcast/ -name "*.so" -type f -delete
	@rm -r docs/build
