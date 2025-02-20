class TouchSlider {
    constructor() {
        this.slider = document.querySelector('.slider');
        this.slides = document.querySelectorAll('.slide');
        this.dots = document.querySelectorAll('.dot');
        this.currentSlide = 0;
        this.startX = 0;
        this.currentX = 0;
        this.isDragging = false;
        this.threshold = 50; // minimum distance to trigger slide change

        this.initializeEvents();
    }

    initializeEvents() {
        // Touch events
        this.slider.addEventListener('touchstart', (e) => this.handleTouchStart(e));
        this.slider.addEventListener('touchmove', (e) => this.handleTouchMove(e));
        this.slider.addEventListener('touchend', () => this.handleTouchEnd());

        // Mouse events (for testing on desktop)
        this.slider.addEventListener('mousedown', (e) => this.handleTouchStart(e));
        this.slider.addEventListener('mousemove', (e) => this.handleTouchMove(e));
        this.slider.addEventListener('mouseup', () => this.handleTouchEnd());
        this.slider.addEventListener('mouseleave', () => this.handleTouchEnd());

        // Dot navigation
        this.dots.forEach((dot, index) => {
            dot.addEventListener('click', () => this.goToSlide(index));
        });
    }

    handleTouchStart(e) {
        this.isDragging = true;
        this.startX = e.type === 'mousedown' ? e.pageX : e.touches[0].pageX;
        this.slider.style.transition = 'none';
    }

    handleTouchMove(e) {
        if (!this.isDragging) return;
        
        e.preventDefault();
        const currentX = e.type === 'mousemove' ? e.pageX : e.touches[0].pageX;
        const diff = currentX - this.startX;
        const offset = -this.currentSlide * 100 + (diff / window.innerWidth * 100);
        
        // Limit sliding to adjacent slides only
        if (offset <= 0 && offset >= -100) {
            this.slider.style.transform = `translateX(${offset}vw)`;
        }
    }

    handleTouchEnd() {
        if (!this.isDragging) return;
        
        this.isDragging = false;
        this.slider.style.transition = 'transform 0.3s ease-out';
        
        const currentX = parseFloat(this.slider.style.transform.replace('translateX(', ''));
        const movement = currentX + (this.currentSlide * 100);

        if (Math.abs(movement) > this.threshold / window.innerWidth * 100) {
            if (movement > 0 && this.currentSlide > 0) {
                this.currentSlide--;
            } else if (movement < 0 && this.currentSlide < this.slides.length - 1) {
                this.currentSlide++;
            }
        }

        this.updateSliderPosition();
    }

    goToSlide(index) {
        this.currentSlide = index;
        this.updateSliderPosition();
    }

    updateSliderPosition() {
        this.slider.style.transform = `translateX(-${this.currentSlide * 100}vw)`;
        
        // Update dots
        this.dots.forEach((dot, index) => {
            dot.classList.toggle('active', index === this.currentSlide);
        });
    }
}
