import { AppBar, Toolbar, Typography, Container, Box, CssBaseline, ThemeProvider, createTheme } from '@mui/material';
import HeroPanel from './components/HeroPanel';

const theme = createTheme({
  palette: {
    primary: {
      main: '#2c3e50',
    },
    secondary: {
      main: '#3498db',
    },
    background: {
      default: '#f8f9fa',
    },
  },
});

function App() {
  return (
    <ThemeProvider theme={theme}>
      <CssBaseline />
      <Box sx={{ flexGrow: 1, m: 0, p: 0, width: '100%' }}>
        {/* Floating Header */}
        <AppBar 
          position="fixed" 
          color="primary"
          elevation={0} 
          sx={{ 
            borderBottom: '1px solid rgba(255,255,255,0.1)' 
          }}
        >
          <Toolbar>
            <Typography variant="h6" component="div" sx={{ flexGrow: 1, fontWeight: 'bold' }}>
              AR
            </Typography>
          </Toolbar>
        </AppBar>
        
        {/* Main Content Area */}
        <HeroPanel />
        
        <Container maxWidth="lg" sx={{ mt: 4 }}>
          <Box sx={{ my: 4 }}>
            <Typography variant="h4" component="h2" gutterBottom>
              About Me
            </Typography>
            <Typography variant="body1" sx={{ fontSize: '1.1rem' }}>
              I am a natural general intelligence specializing in building scalable software and exploring mechanistic interpretability.
            </Typography>
          </Box>
        </Container>
      </Box>
    </ThemeProvider>
  );
}

export default App;
