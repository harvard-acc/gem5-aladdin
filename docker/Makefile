.PHONY: build run

build:
	docker build . -f Dockerfile -t gem5-aladdin:dev

run:
	docker run -it --rm \
		--mount source=gem5-aladdin-workspace,target=/workspace \
		gem5-aladdin:dev
